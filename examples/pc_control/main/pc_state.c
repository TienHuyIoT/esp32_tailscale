#include "pc_state.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static pc_state_snapshot_t s_state;
static SemaphoreHandle_t s_lock;

static void lock(void) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void) {
    if (s_lock) xSemaphoreGive(s_lock);
}

static void copy_str(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

void pc_state_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.active.state = PC_CMD_STATE_NONE;
    copy_str(s_state.power_source, sizeof(s_state.power_source), "none");
    copy_str(s_state.power_note, sizeof(s_state.power_note),
             "no power-state signal available");
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void pc_state_get(pc_state_snapshot_t *out) {
    if (!out) return;
    lock();
    *out = s_state;
    unlock();
}

void pc_state_set_power(pc_power_state_t p, const char *source, const char *note) {
    lock();
    s_state.power = p;
    s_state.power_updated_us = esp_timer_get_time();
    copy_str(s_state.power_source, sizeof(s_state.power_source),
             source ? source : "none");
    copy_str(s_state.power_note, sizeof(s_state.power_note), note ? note : "");
    unlock();
}

bool pc_state_activate(const pc_cmd_slot_t *slot, pc_cmd_slot_t *active_out) {
    bool ok = false;
    lock();
    if (s_state.active.state == PC_CMD_STATE_NONE) {
        s_state.active = *slot;
        s_state.active.state = PC_CMD_STATE_ACCEPTED;
        ok = true;
    } else if (active_out) {
        *active_out = s_state.active;
    }
    unlock();
    return ok;
}

bool pc_state_active_update(const char *request_id, pc_cmd_state_t state,
                            const char *detail, const char *error,
                            int64_t deadline_us_set) {
    bool applied = false;
    if (!request_id) return false;
    lock();
    if (s_state.active.state != PC_CMD_STATE_NONE &&
        strcmp(s_state.active.request_id, request_id) == 0) {
        s_state.active.state = state;
        s_state.active.updated_us = esp_timer_get_time();
        if (deadline_us_set > 0) s_state.active.deadline_us = deadline_us_set;
        if (detail) copy_str(s_state.active.detail, sizeof(s_state.active.detail), detail);
        if (error) copy_str(s_state.active.error, sizeof(s_state.active.error), error);
        applied = true;
    }
    unlock();
    return applied;
}

void pc_state_active_finish(const char *request_id) {
    if (!request_id) return;
    lock();
    if (s_state.active.state != PC_CMD_STATE_NONE &&
        strcmp(s_state.active.request_id, request_id) == 0) {
        /* newest first */
        for (int i = PC_STATE_HISTORY - 1; i > 0; i--) {
            s_state.history[i] = s_state.history[i - 1];
        }
        s_state.history[0] = s_state.active;
        if (s_state.history_count < PC_STATE_HISTORY) s_state.history_count++;
        memset(&s_state.active, 0, sizeof(s_state.active));
        s_state.active.state = PC_CMD_STATE_NONE;
    }
    unlock();
}

bool pc_state_find(const char *request_id, pc_cmd_slot_t *out) {
    bool found = false;
    if (!request_id || !out) return false;
    lock();
    if (s_state.active.state != PC_CMD_STATE_NONE &&
        strcmp(s_state.active.request_id, request_id) == 0) {
        *out = s_state.active;
        found = true;
    } else {
        for (int i = 0; i < s_state.history_count; i++) {
            if (strcmp(s_state.history[i].request_id, request_id) == 0) {
                *out = s_state.history[i];
                found = true;
                break;
            }
        }
    }
    unlock();
    return found;
}

const char *pc_power_state_str(pc_power_state_t p) {
    switch (p) {
    case PC_PWR_ONLINE:   return "online";
    case PC_PWR_OFFLINE:  return "offline";
    case PC_PWR_SLEEPING: return "sleeping";
    default:              return "unknown";
    }
}
