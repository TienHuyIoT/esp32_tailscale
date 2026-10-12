/**
 * @file pc_cmd.h
 * @brief PC-control command identifiers, routes and lifecycle states.
 *
 * Dependency-free (no ESP-IDF includes) so pc_state / pc_hw / pc_win_client /
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
    PC_CMD_SHUTDOWN,
    PC_CMD_RESTART,
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

typedef enum {
    PC_CMD_ROUTE_HARDWARE = 0,  /* pc_hw abstraction (mock or GPIO) */
    PC_CMD_ROUTE_WINDOWS,       /* Windows agent over the tailnet */
} pc_cmd_route_t;

typedef struct {
    pc_cmd_id_t id;
    const char *name;            /* exact API identifier, e.g. "POWER_ON" */
    pc_cmd_route_t route;
    bool requires_confirm;       /* request must echo confirm == name */
    bool cancellable;            /* may be cancelled before dispatch/confirm */
    const char *description;
} pc_cmd_def_t;

/** Definition for a command id (never NULL for valid ids). */
const pc_cmd_def_t *pc_cmd_def(pc_cmd_id_t id);

/** Look up by exact API name; NULL when unknown. */
const pc_cmd_def_t *pc_cmd_lookup(const char *name);

/** Lower-case lifecycle state string for JSON/logs ("accepted", ...). */
const char *pc_cmd_state_str(pc_cmd_state_t state);

/** Route string for JSON ("hardware" / "windows"). */
const char *pc_cmd_route_str(pc_cmd_route_t route);

#ifdef __cplusplus
}
#endif
