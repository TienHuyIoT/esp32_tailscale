/**
 * @file pc_controller.h
 * @brief PC-control orchestration: command queue, duplicate protection,
 *        timeouts and hardware cooldown.
 *
 * A single dedicated task ("pc_ctl") owns all command execution; HTTP
 * handlers only validate + submit and read snapshots, so the shared httpd
 * task never blocks on the (blocking) hardware pulse.
 */

#pragma once

#include "esp_err.h"
#include "microlink.h"
#include "pc_cmd.h"
#include "pc_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_SUBMIT_OK = 0,         /* accepted; out_slot filled (state "accepted") */
    PC_SUBMIT_REPLAY,         /* same request_id exists; out_slot = existing */
    PC_SUBMIT_BUSY,           /* another command active; out_slot = active one */
    PC_SUBMIT_COOLDOWN,       /* hardware cooldown; out_retry_ms filled */
    PC_SUBMIT_UNAVAILABLE,    /* route unusable now; out_reason explains */
    PC_SUBMIT_DISABLED,       /* function disabled in the device Settings */
    PC_SUBMIT_GPIO_UNCONFIGURED, /* hardware function without a GPIO mapping */
    PC_SUBMIT_INTERNAL_ERROR, /* queue/alloc failure */
} pc_submit_status_t;

typedef struct {
    pc_cmd_id_t id;
    const char *request_id;      /* optional; NULL/empty = auto-generate */
    uint32_t hold_ms;            /* FORCE_POWER_OFF override (0 = default) */

    /* outputs (all optional) */
    pc_cmd_slot_t *out_slot;
    uint32_t *out_retry_ms;
    const char **out_reason;
} pc_submit_req_t;

/** Start the pc_ctl task. Call after microlink_start(). */
esp_err_t pc_controller_start(microlink_t *ml);

pc_submit_status_t pc_controller_submit(const pc_submit_req_t *req);

bool pc_controller_get(const char *request_id, pc_cmd_slot_t *out_slot);

/** Remaining hardware cooldown in ms (0 = hardware commands ready).
 *  Mirrors the submit-time check, so the UI countdown can never unlock
 *  earlier than the firmware would accept (reported in /api/v1/status). */
uint32_t pc_controller_hw_cooldown_remaining_ms(void);

/** Availability for GET /api/v1/commands (reason is a static string). */
void pc_controller_command_availability(const pc_cmd_def_t *def,
                                        bool *available, const char **reason);

#ifdef __cplusplus
}
#endif
