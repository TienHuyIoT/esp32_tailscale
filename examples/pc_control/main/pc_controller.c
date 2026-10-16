/**
 * @file pc_controller.c
 * @brief PC-control orchestration implementation (single pc_ctl task).
 *
 * Every command drives the motherboard front-panel inputs through the real
 * GPIO layer (pc_hw.c) - there is no simulation mode.
 *
 * Safety invariants:
 *  - one active command at a time; same request_id replays, a different id
 *    while busy is rejected (409 at the API layer);
 *  - hardware commands have a configurable cooldown even across ids;
 *  - FORCE_POWER_OFF runs only when explicitly requested.
 */

#include "pc_controller.h"
#include "pc_hw.h"
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

typedef struct {
    char request_id[PC_ID_MAX];
    pc_cmd_id_t id;
    uint32_t hold_ms;
} pc_job_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static bool s_hw_ready;           /* pc_hw_init() succeeded */
static int64_t s_last_hw_us;      /* cooldown anchor (0 = never) */

uint32_t pc_controller_hw_cooldown_remaining_ms(void) {
    int64_t last = s_last_hw_us;                   /* 0 = never ran a hw cmd */
    if (last == 0) return 0;
    int64_t elapsed_ms = (esp_timer_get_time() - last) / 1000;
    if (elapsed_ms < 0) elapsed_ms = 0;            /* defensive: clock jumps */
    if (elapsed_ms >= CONFIG_PCCTL_HW_COOLDOWN_MS) return 0;
    return (uint32_t)(CONFIG_PCCTL_HW_COOLDOWN_MS - elapsed_ms);
}

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

    int slot = pc_cmd_gpio_slot(def->id);
    if (slot < 0 || cfg.gpio[slot] < 0) {
        *available = false;
        *reason = "GPIO not configured (Settings -> GPIO mapping)";
        return;
    }
    if (!s_hw_ready) {
        *available = false;
        *reason = "GPIO hardware layer not initialised (check the boot log)";
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

    /* Part 2: every function needs a configured GPIO mapping. */
    {
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
    if (s_last_hw_us != 0) {
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

static void finish_command(const char *id, pc_cmd_state_t state,
                           const char *detail, const char *error) {
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
        .active_high = (slot >= 0) && (cfg.level[slot] == PC_CFG_LEVEL_HIGH),
        .pulse_ms = cfg.short_press_ms,
        .hold_ms = (job->id == PC_CMD_FORCE_POWER_OFF && job->hold_ms)
                       ? job->hold_ms : cfg.long_press_ms,
    };
    if (params.gpio_pin < 0) {
        finish_command(job->request_id, PC_CMD_STATE_FAILED, NULL,
                       "GPIO not configured for this function");
        return;
    }
    uint32_t dur = pc_hw_duration_ms(job->id, &params);
    int64_t deadline = esp_timer_get_time() + (int64_t)(dur + 2000) * 1000;

    if (!pc_state_active_update(job->request_id, PC_CMD_STATE_RUNNING,
                                "issuing hardware action", NULL, deadline)) {
        return;   /* finished meanwhile */
    }
    char detail[PC_DETAIL_MAX] = "";
    esp_err_t err = pc_hw_execute(job->id, &params, detail, sizeof(detail));
    s_last_hw_us = esp_timer_get_time();

    if (err == ESP_OK) {
        finish_command(job->request_id, PC_CMD_STATE_SUCCEEDED, detail, NULL);
    } else {
        finish_command(job->request_id, PC_CMD_STATE_FAILED, detail,
                       esp_err_to_name(err));
    }
}

static void handle_job(const pc_job_t *job) {
    execute_hw_command(job);
}

static void pc_ctl_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "pc_ctl task started (hw=%s)", s_hw_ready ? "gpio" : "unavailable");

    for (;;) {
        /* supervision: deadline expiry */
        /* static: keeps the multi-KB snapshot off the task stack */
        static pc_state_snapshot_t snap;
        pc_state_get(&snap);
        if (snap.active.state == PC_CMD_STATE_RUNNING && snap.active.deadline_us > 0 &&
            esp_timer_get_time() > snap.active.deadline_us) {
            finish_command(snap.active.request_id, PC_CMD_STATE_TIMEOUT,
                           "deadline exceeded", NULL);
        }

        pc_job_t job;
        if (xQueueReceive(s_queue, &job, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle_job(&job);
        }
    }
}

esp_err_t pc_controller_start(microlink_t *ml) {
    if (!ml) return ESP_ERR_INVALID_ARG;
    if (s_task) return ESP_OK;
    (void)ml;   /* the controller no longer talks to the network itself */

    pc_state_init();

    /* real GPIO hardware layer (the only implementation) */
    esp_err_t herr = pc_hw_init();
    if (herr != ESP_OK) {
        ESP_LOGE(TAG, "hardware init failed: %s", esp_err_to_name(herr));
        s_hw_ready = false;
    } else {
        s_hw_ready = true;
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

