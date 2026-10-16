/**
 * @file pc_cmd.h
 * @brief PC-control command identifiers and lifecycle states.
 *
 * Every command is a hardware front-panel action (PWR_SW / RESET_SW). The
 * former SHUTDOWN / RESTART software commands and their Windows-agent route
 * were removed - the device only ever pulses the motherboard inputs.
 *
 * Dependency-free (no ESP-IDF includes) so pc_state / pc_hw /
 * pc_controller / pc_api can all share it cheaply.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_CMD_POWER_ON = 0,
    PC_CMD_WAKE_UP,
    PC_CMD_PWR_RESET,
    PC_CMD_FORCE_POWER_OFF,
    PC_CMD_COUNT
} pc_cmd_id_t;

/* Command lifecycle. Monotonic: a command never returns to an earlier state.
 *
 *   accepted -> running -> succeeded | failed | timeout | cancelled
 */
typedef enum {
    PC_CMD_STATE_NONE = 0,   /* slot empty */
    PC_CMD_STATE_ACCEPTED,
    PC_CMD_STATE_RUNNING,
    PC_CMD_STATE_SUCCEEDED,
    PC_CMD_STATE_FAILED,
    PC_CMD_STATE_TIMEOUT,
    PC_CMD_STATE_CANCELLED,
} pc_cmd_state_t;

typedef struct {
    pc_cmd_id_t id;
    const char *name;            /* exact API identifier, e.g. "POWER_ON" */
    bool requires_confirm;       /* request must echo confirm == name */
    const char *description;
} pc_cmd_def_t;

/** Definition for a command id (never NULL for valid ids). */
const pc_cmd_def_t *pc_cmd_def(pc_cmd_id_t id);

/** Look up by exact API name; NULL when unknown. */
const pc_cmd_def_t *pc_cmd_lookup(const char *name);

/** Lower-case lifecycle state string for JSON/logs ("accepted", ...). */
const char *pc_cmd_state_str(pc_cmd_state_t state);

#ifdef __cplusplus
}
#endif
