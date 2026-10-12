/**
 * @file pc_controller.c
 * @brief PC-control orchestration implementation (single pc_ctl task).
 *
 * Routing (kept strictly separate):
 *   POWER_ON / WAKE_UP / PWR_RESET / FORCE_POWER_OFF -> pc_hw abstraction
 *   SHUTDOWN / RESTART                               -> Windows agent
 *
 * Safety invariants:
 *  - one active command at a time; same request_id replays, a different id
 *    while busy is rejected (409 at the API layer);
 *  - hardware commands have a configurable cooldown even across ids;
 *  - FORCE_POWER_OFF runs only when explicitly requested (never
 *    automatically because the agent is unreachable);
 *  - the firmware can report the agent's dry-run state but cannot change it.
 */

#include "pc_controller.h"
#include "pc_hw.h"
#include "pc_win_client.h"
#include "pc_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_ctl";

#define PC_QUEUE_DEPTH 4
#define PC_TASK_STACK  8192
#define PC_TASK_PRIO   5

#if CONFIG_PCCTL_EXEC_MODE_MOCK_ALL
#define PC_MODE_STR "MOCK"
#define PC_HW_MOCK  1
#define PC_WIN_MOCK 1
#elif CONFIG_PCCTL_EXEC_MODE_MIXED
#define PC_MODE_STR "MIXED"
#define PC_HW_MOCK  1
#define PC_WIN_MOCK 0
#else /* CONFIG_PCCTL_EXEC_MODE_FULL */
#define PC_MODE_STR "HARDWARE"
#define PC_HW_MOCK  0
#define PC_WIN_MOCK 0
#endif

typedef enum { PC_JOB_COMMAND = 0, PC_JOB_CANCEL_AGENT, PC_JOB_TEST } pc_job_type_t;

typedef struct {
    pc_job_type_t type;
    char request_id[PC_ID_MAX];
    pc_cmd_id_t id;
    uint32_t hold_ms;
} pc_job_t;

static microlink_t *s_ml;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static const pc_hw_ops_t *s_hw_ops;
static bool s_hw_available;
static int64_t s_last_hw_us;      /* cooldown anchor (0 = never) */
static int64_t s_next_poll_us;

static bool win_mock_mode(void) { return PC_WIN_MOCK; }

const char *pc_controller_mode_str(void) { return PC_MODE_STR; }

void pc_controller_command_availability(const pc_cmd_def_t *def,
                                        bool *available, const char **reason) {
    *available = true;
    *reason = NULL;
    if (!def) {
        *available = false;
        *reason = "unknown command";
        return;
    }

    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);

    /* Firmware-side permission check: a disabled function is unavailable in
     * every mode - the Settings UI is never the only enforcement point. */
    if (!pc_cmd_function_enabled(&cfg, def->id)) {
        *available = false;
        *reason = "disabled in device Settings";
        return;
    }

    if (def->route == PC_CMD_ROUTE_HARDWARE) {
        int slot = pc_cmd_gpio_slot(def->id);
        if (slot < 0 || cfg.gpio[slot] < 0) {
            *available = false;
            *reason = "GPIO not configured (Settings -> GPIO mapping)";
            return;
        }
        if (!s_hw_available) {
            *available = false;
            *reason = "GPIO hardware mode is enabled but not usable "
                      "(check CONFIG_PCCTL_ENABLE_HW_GPIO / wiring)";
        }
        return;
    }

    /* Windows route */
    if (win_mock_mode()) return;    /* firmware-simulated in MOCK mode */
    if (!cfg.token_set) {
        *available = false;
        *reason = "agent token not configured (device Settings)";
        return;
    }
    if (!microlink_is_connected(s_ml)) {
        *available = false;
        *reason = "tailnet not connected";
        return;
    }
    pc_state_snapshot_t snap;
    pc_state_get(&snap);
    if (snap.service == PC_SVC_OFFLINE) {
        *available = false;
        *reason = "Windows agent unreachable";
    }
}

static void gen_request_id(char *out, size_t out_len) {
    for (int attempt = 0; attempt < 4; attempt++) {
        snprintf(out, out_len, "c-%08lx%04x",
                 (unsigned long)((uint64_t)esp_timer_get_time() & 0xFFFFFFFF),
                 (unsigned)(esp_random() & 0xFFFF));
        pc_cmd_slot_t tmp;
        if (!pc_state_find(out, &tmp)) return;
    }
}

pc_submit_status_t pc_controller_submit(const pc_submit_req_t *req) {
    if (!req) return PC_SUBMIT_INTERNAL_ERROR;
    const pc_cmd_def_t *def = pc_cmd_def(req->id);
    if (!def) return PC_SUBMIT_INTERNAL_ERROR;

    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);

    /* Common command guard, part 1: exists + enabled (firmware-side). */
    if (!pc_cmd_function_enabled(&cfg, def->id)) {
        return PC_SUBMIT_DISABLED;
    }

    /* Part 2: hardware functions need a configured GPIO mapping. */
    if (def->route == PC_CMD_ROUTE_HARDWARE) {
        int slot = pc_cmd_gpio_slot(def->id);
        if (slot < 0 || cfg.gpio[slot] < 0) {
            if (req->out_reason) *req->out_reason = "GPIO not configured for this function";
            return PC_SUBMIT_GPIO_UNCONFIGURED;
        }
    }

    bool available;
    const char *reason;
    pc_controller_command_availability(def, &available, &reason);
    if (!available) {
        if (req->out_reason) *req->out_reason = reason;
        return PC_SUBMIT_UNAVAILABLE;
    }

    /* Hardware cooldown: duplicate-request protection across ids. */
    if (def->route == PC_CMD_ROUTE_HARDWARE && s_last_hw_us != 0) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_last_hw_us) / 1000;
        if (elapsed_ms < CONFIG_PCCTL_HW_COOLDOWN_MS) {
            if (req->out_retry_ms) {
                *req->out_retry_ms = (uint32_t)(CONFIG_PCCTL_HW_COOLDOWN_MS - elapsed_ms);
            }
            return PC_SUBMIT_COOLDOWN;
        }
    }

    char id[PC_ID_MAX] = {0};
    if (req->request_id && req->request_id[0]) {
        snprintf(id, sizeof(id), "%s", req->request_id);
        pc_cmd_slot_t existing;
        if (pc_state_find(id, &existing)) {
            if (req->out_slot) *req->out_slot = existing;
            return PC_SUBMIT_REPLAY;
        }
    } else {
        gen_request_id(id, sizeof(id));
    }

    pc_cmd_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    snprintf(slot.request_id, sizeof(slot.request_id), "%s", id);
    slot.id = req->id;
    slot.state = PC_CMD_STATE_ACCEPTED;
    slot.created_us = slot.updated_us = esp_timer_get_time();
    snprintf(slot.detail, sizeof(slot.detail), "queued for execution");

    pc_cmd_slot_t active;
    if (!pc_state_activate(&slot, &active)) {
        if (req->out_slot) *req->out_slot = active;
        return PC_SUBMIT_BUSY;
    }

    pc_job_t job;
    memset(&job, 0, sizeof(job));
    job.type = PC_JOB_COMMAND;
    job.id = req->id;
    job.hold_ms = req->hold_ms;
    snprintf(job.request_id, sizeof(job.request_id), "%s", id);

    if (xQueueSend(s_queue, &job, 0) != pdTRUE) {
        pc_state_active_update(id, PC_CMD_STATE_FAILED, NULL, "internal queue full", 0);
        pc_state_active_finish(id);
        if (req->out_slot) pc_state_find(id, req->out_slot);
        return PC_SUBMIT_INTERNAL_ERROR;
    }

    if (req->out_slot) *req->out_slot = slot;
    ESP_LOGI(TAG, "command accepted: %s id=%s", def->name, id);
    return PC_SUBMIT_OK;
}

pc_cancel_status_t pc_controller_cancel(const char *request_id,
                                        pc_cmd_slot_t *out_slot,
                                        char *reason, size_t reason_len) {
    pc_cmd_slot_t cur;
    if (!request_id || !pc_state_find(request_id, &cur) ||
        cur.state == PC_CMD_STATE_NONE) {
        return PC_CANCEL_NOT_FOUND;
    }
    const pc_cmd_def_t *def = pc_cmd_def(cur.id);

    if (def && def->cancellable && cur.state == PC_CMD_STATE_ACCEPTED) {
        pc_state_active_update(request_id, PC_CMD_STATE_CANCELLED,
                               "cancelled before dispatch", NULL, 0);
        pc_state_active_finish(request_id);
        if (out_slot) pc_state_find(request_id, out_slot);
        return PC_CANCEL_OK;
    }

    if (def && def->cancellable && cur.state == PC_CMD_STATE_RUNNING &&
        def->route == PC_CMD_ROUTE_WINDOWS && !win_mock_mode()) {
        pc_job_t job;
        memset(&job, 0, sizeof(job));
        job.type = PC_JOB_CANCEL_AGENT;
        job.id = cur.id;
        snprintf(job.request_id, sizeof(job.request_id), "%s", request_id);
        if (xQueueSend(s_queue, &job, 0) == pdTRUE) {
            if (out_slot) *out_slot = cur;
            if (reason && reason_len) {
                snprintf(reason, reason_len,
                         "cancel forwarded to the Windows agent - check command status");
            }
            return PC_CANCEL_FORWARDED;
        }
    }

    if (out_slot) *out_slot = cur;
    if (reason && reason_len) {
        if (cur.state == PC_CMD_STATE_RUNNING) {
            snprintf(reason, reason_len,
                     "action already in progress beyond the cancellation point");
        } else {
            snprintf(reason, reason_len,
                     "command is no longer cancellable (state: %s)",
                     pc_cmd_state_str(cur.state));
        }
    }
    return PC_CANCEL_NOT_CANCELLABLE;
}

static void finish_command(const char *id, pc_cmd_state_t state,
                           const char *detail, const char *error, bool simulated) {
    if (simulated) pc_state_active_set_simulated(id, true);
    pc_state_active_update(id, state, detail, error, 0);
    pc_state_active_finish(id);
}

static void execute_hw_command(const pc_job_t *job) {
    /* Runtime pin mapping + timings from the Settings UI (NVS). */
    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);
    int slot = pc_cmd_gpio_slot(job->id);
    pc_hw_params_t params = {
        .gpio_pin = (slot >= 0) ? cfg.gpio[slot] : -1,
        .pulse_ms = cfg.short_press_ms,
        .hold_ms = (job->id == PC_CMD_FORCE_POWER_OFF && job->hold_ms)
                       ? job->hold_ms : cfg.long_press_ms,
    };
    if (params.gpio_pin < 0) {
        finish_command(job->request_id, PC_CMD_STATE_FAILED, NULL,
                       "GPIO not configured for this function", false);
        return;
    }
    uint32_t dur = s_hw_ops->duration_ms(job->id, &params);
    int64_t deadline = esp_timer_get_time() + (int64_t)(dur + 2000) * 1000;

    if (!pc_state_active_update(job->request_id, PC_CMD_STATE_RUNNING,
                                "issuing hardware action", NULL, deadline)) {
        return;   /* cancelled/finished meanwhile */
    }
    char detail[PC_DETAIL_MAX] = "";
    esp_err_t err = s_hw_ops->execute(job->id, &params, detail, sizeof(detail));
    s_last_hw_us = esp_timer_get_time();

    if (err == ESP_OK) {
        finish_command(job->request_id, PC_CMD_STATE_SUCCEEDED, detail, NULL, PC_HW_MOCK);
    } else {
        finish_command(job->request_id, PC_CMD_STATE_FAILED, detail,
                       esp_err_to_name(err), PC_HW_MOCK);
    }
}

static void execute_windows_command(const pc_job_t *job) {
    const pc_cmd_def_t *def = pc_cmd_def(job->id);

    if (win_mock_mode()) {
        if (!pc_state_active_update(job->request_id, PC_CMD_STATE_RUNNING,
                                    "simulating Windows request (MOCK mode)", NULL, 0)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(800));
        char detail[PC_DETAIL_MAX];
        snprintf(detail, sizeof(detail),
                 "simulated %s (MOCK mode: no request was sent to the Windows agent)",
                 def ? def->name : "?");
        finish_command(job->request_id, PC_CMD_STATE_SUCCEEDED, detail, NULL, true);
        return;
    }

    int64_t deadline = esp_timer_get_time() +
                       (int64_t)(CONFIG_PCCTL_HTTP_TIMEOUT_MS + 3000) * 1000;
    if (!pc_state_active_update(job->request_id, PC_CMD_STATE_RUNNING,
                                "contacting Windows agent", NULL, deadline)) {
        return;
    }

    pc_win_result_t res;
    bool ok = (job->id == PC_CMD_SHUTDOWN)
                  ? pc_win_client_post_shutdown(CONFIG_PCCTL_SHUTDOWN_DELAY_S, &res)
                  : pc_win_client_post_restart(CONFIG_PCCTL_SHUTDOWN_DELAY_S, &res);
    pc_win_client_set_fast_fail(!ok && res.http_status <= 0);

    if (ok) {
        char detail[PC_DETAIL_MAX];
        if (res.dry_run) {
            snprintf(detail, sizeof(detail),
                     "agent accepted %s request (dry-run: nothing real was scheduled)",
                     def ? def->name : "?");
        } else if (res.pending) {
            snprintf(detail, sizeof(detail),
                     "agent scheduled %s (about %ld s remaining)",
                     def ? def->name : "?", (long)res.remaining_s);
        } else {
            snprintf(detail, sizeof(detail), "agent accepted %s request",
                     def ? def->name : "?");
        }
        pc_state_set_service(PC_SVC_ONLINE, res.dry_run, NULL);
        if (!res.dry_run && res.pending) {
            pc_state_set_power(job->id == PC_CMD_SHUTDOWN ? PC_PWR_SHUTTING_DOWN
                                                          : PC_PWR_RESTARTING,
                               "agent", "agent confirmed the operation is scheduled");
        } else {
            pc_state_set_power(PC_PWR_ONLINE, "agent",
                res.dry_run ? "agent reachable (dry-run; PC state is an inference)"
                            : "agent reachable (inference, no direct power sensing)");
        }
        finish_command(job->request_id, PC_CMD_STATE_SUCCEEDED, detail, NULL, res.dry_run);
    } else {
        /* Distinguish "agent answered with a structured error" (agent IS
         * reachable - HTTP 4xx from a live agent) from transport failure. */
        char fail_detail[PC_DETAIL_MAX];
        if (res.pending && res.operation[0]) {
            snprintf(fail_detail, sizeof(fail_detail),
                     "agent reports '%s' already pending (about %ld s remaining)",
                     res.operation, (long)res.remaining_s);
        } else if (res.http_status == 401) {
            snprintf(fail_detail, sizeof(fail_detail),
                     "agent rejected the credentials (HTTP 401)");
        } else {
            snprintf(fail_detail, sizeof(fail_detail),
                     "agent request failed (HTTP %d)", res.http_status);
        }
        if (res.http_status > 0) {
            pc_state_set_service(PC_SVC_ONLINE, res.dry_run, NULL);
        } else {
            pc_state_set_service(PC_SVC_OFFLINE, false, res.error);
        }
        finish_command(job->request_id, PC_CMD_STATE_FAILED, fail_detail, res.error, false);
    }
}

static void execute_cancel_agent(const pc_job_t *job) {
    pc_win_result_t res;
    bool ok = pc_win_client_post_cancel(&res);

    pc_cmd_slot_t cur;
    if (!pc_state_find(job->request_id, &cur)) return;
    if (cur.state != PC_CMD_STATE_RUNNING) {
        ESP_LOGI(TAG, "cancel: command %s already finished (state %s)",
                 job->request_id, pc_cmd_state_str(cur.state));
        return;
    }
    if (ok && res.cancelled) {
        finish_command(job->request_id, PC_CMD_STATE_CANCELLED,
                       "cancelled via Windows agent", NULL, false);
    } else {
        char detail[PC_DETAIL_MAX];
        snprintf(detail, sizeof(detail), "agent cancel not possible: %.80s",
                 res.reason[0] ? res.reason : (ok ? "nothing pending" : res.error));
        pc_state_active_update(job->request_id, PC_CMD_STATE_RUNNING, detail, NULL, 0);
    }
}

/* One-shot connection test from the Settings UI: verifies reachability AND
 * authentication against the configured agent (its GET /api/v1/status is
 * bearer-token protected, so a 200 proves both). Result lands in pc_state
 * for the UI to poll; nothing is executed on the PC. */
static void execute_connection_test(void) {
    pc_win_result_t res;
    bool ok = pc_win_client_get_status(&res);
    pc_win_client_set_fast_fail(!ok && res.http_status <= 0);

    bool reachable = res.http_status > 0;
    bool authenticated = ok;
    char err[PC_ERROR_MAX] = "";
    if (!ok) {
        snprintf(err, sizeof(err), "%s", res.error[0] ? res.error : "test failed");
    }
    pc_state_set_test(reachable, authenticated, res.http_status, err);
    if (reachable) {
        pc_state_set_service(PC_SVC_ONLINE, res.dry_run, NULL);
    } else {
        pc_state_set_service(PC_SVC_OFFLINE, false, err);
    }
    ESP_LOGI(TAG, "connection test: reachable=%d authenticated=%d http=%d",
             (int)reachable, (int)authenticated, res.http_status);
}

static void poll_agent(void) {
    /* static: keeps the multi-KB snapshot off the task stack */
    static pc_state_snapshot_t snap;
    pc_state_get(&snap);

    if (!microlink_is_connected(s_ml)) {
        pc_state_set_service_unknown("tailnet not connected - cannot reach agent");
        return;
    }

    pc_win_result_t res;
    bool ok = pc_win_client_get_status(&res);
    pc_win_client_set_fast_fail(!ok);
    if (!ok) {
        bool was_pending = (snap.power == PC_PWR_SHUTTING_DOWN ||
                            snap.power == PC_PWR_RESTARTING);
        pc_state_set_service(PC_SVC_OFFLINE, false, res.error);
        if (was_pending) {
            pc_state_set_power(PC_PWR_UNKNOWN, "agent_lost",
                "agent unreachable after shutdown/restart request - PC state unknown");
        } else if (snap.power == PC_PWR_ONLINE &&
                   strcmp(snap.power_source, "agent") == 0) {
            /* reachability-based inference goes stale once the agent is gone */
            pc_state_set_power(PC_PWR_UNKNOWN, "agent_lost",
                "agent unreachable - previous online state was an inference");
        }
        return;
    }

    pc_state_set_service(PC_SVC_ONLINE, res.dry_run, NULL);
    if (res.pending && res.operation[0]) {
        if (res.dry_run) {
            /* a dry-run pending is NOT a real scheduled action */
            pc_state_set_power(PC_PWR_ONLINE, "agent",
                "agent has a DRY-RUN operation pending (no real action scheduled)");
        } else {
            pc_power_state_t p = (strcmp(res.operation, "restart") == 0)
                                 ? PC_PWR_RESTARTING : PC_PWR_SHUTTING_DOWN;
            pc_state_set_power(p, "agent", "agent reports the operation is pending");
        }
    } else if (snap.power == PC_PWR_SHUTTING_DOWN || snap.power == PC_PWR_RESTARTING) {
        pc_state_set_power(PC_PWR_UNKNOWN, "agent",
                           "pending operation no longer reported by the agent");
    } else {
        pc_state_set_power(PC_PWR_ONLINE, "agent",
            res.dry_run ? "agent reachable (dry-run; PC state is an inference)"
                        : "agent reachable (inference, no direct power sensing)");
    }
}

static void handle_job(const pc_job_t *job) {
    if (job->type == PC_JOB_CANCEL_AGENT) {
        execute_cancel_agent(job);
    } else if (job->type == PC_JOB_TEST) {
        execute_connection_test();
    } else {
        const pc_cmd_def_t *def = pc_cmd_def(job->id);
        if (def && def->route == PC_CMD_ROUTE_WINDOWS) {
            execute_windows_command(job);
        } else {
            execute_hw_command(job);
        }
    }
}

static void pc_ctl_task(void *arg) {
    ESP_LOGI(TAG, "pc_ctl task started (mode=%s, hw_ops=%s)",
             pc_controller_mode_str(), s_hw_ops ? s_hw_ops->name : "none");
    s_next_poll_us = esp_timer_get_time() + 500 * 1000;   /* first poll quickly */

    for (;;) {
        int64_t now = esp_timer_get_time();

        /* Commands first (non-blocking): a queued command must never wait
         * behind an agent poll, which can block for seconds while the
         * Windows agent is down. */
        pc_job_t job;
        while (xQueueReceive(s_queue, &job, 0) == pdTRUE) {
            handle_job(&job);
            now = esp_timer_get_time();
        }

        /* supervision: deadline expiry */
        /* static: keeps the multi-KB snapshot off the task stack */
        static pc_state_snapshot_t snap;
        pc_state_get(&snap);
        if (snap.active.state == PC_CMD_STATE_RUNNING && snap.active.deadline_us > 0 &&
            now > snap.active.deadline_us) {
            finish_command(snap.active.request_id, PC_CMD_STATE_TIMEOUT,
                           "deadline exceeded", NULL, false);
        }

        if (now >= s_next_poll_us) {
            int64_t interval_us = (int64_t)CONFIG_PCCTL_POLL_INTERVAL_S * 1000000;
            if (win_mock_mode()) {
                /* MOCK: nothing to ask the agent. Skipping the poll avoids
                 * multi-second blocking TCP attempts (agent usually not
                 * running yet) that would delay queued commands. */
                pc_state_set_service_unknown("MOCK mode - Windows agent is not contacted");
            } else {
                poll_agent();
                /* Offline back-off: fewer blocking connect attempts while
                 * the agent is down (doubles, capped at 30 s so recovery
                 * is still noticed quickly). */
                pc_state_snapshot_t post;
                pc_state_get(&post);
                if (post.service == PC_SVC_OFFLINE) {
                    interval_us *= 2;
                    if (interval_us > 30LL * 1000000) interval_us = 30LL * 1000000;
                }
            }
            s_next_poll_us = esp_timer_get_time() + interval_us;
        }

        int64_t wait_us = s_next_poll_us - esp_timer_get_time();
        if (wait_us < 100 * 1000) wait_us = 100 * 1000;
        TickType_t wait_ticks = pdMS_TO_TICKS((wait_us + 999) / 1000);
        if (wait_ticks > pdMS_TO_TICKS(1000)) wait_ticks = pdMS_TO_TICKS(1000);

        if (xQueueReceive(s_queue, &job, wait_ticks) == pdTRUE) {
            handle_job(&job);
        }
    }
}

esp_err_t pc_controller_start(microlink_t *ml) {
    if (!ml) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_OK;
    s_ml = ml;

    pc_state_init();

    /* hardware implementation selection */
#if PC_HW_MOCK
    s_hw_ops = &pc_hw_mock_ops;
    s_hw_available = true;
#else
    #if CONFIG_PCCTL_ENABLE_HW_GPIO
    s_hw_ops = &pc_hw_gpio_ops;
    s_hw_available = true;
    #else
    s_hw_ops = &pc_hw_mock_ops;   /* never executed: FULL without GPIO is down */
    s_hw_available = false;
    #endif
#endif

    esp_err_t err = pc_win_client_setup(ml);
    if (err != ESP_OK) return err;

    if (s_hw_ops && s_hw_ops->init) {
        esp_err_t herr = s_hw_ops->init();
        if (herr != ESP_OK) {
            ESP_LOGE(TAG, "hardware init failed: %s", esp_err_to_name(herr));
            s_hw_available = false;
        }
    }

    s_queue = xQueueCreate(PC_QUEUE_DEPTH, sizeof(pc_job_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(pc_ctl_task, "pc_ctl", PC_TASK_STACK, NULL, PC_TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool pc_controller_get(const char *request_id, pc_cmd_slot_t *out_slot) {
    return pc_state_find(request_id, out_slot);
}

esp_err_t pc_controller_request_test(void) {
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    pc_job_t job;
    memset(&job, 0, sizeof(job));
    job.type = PC_JOB_TEST;
    if (xQueueSend(s_queue, &job, 0) != pdTRUE) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "connection test queued");
    return ESP_OK;
}
