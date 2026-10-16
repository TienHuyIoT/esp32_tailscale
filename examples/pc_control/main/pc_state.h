/**
 * @file pc_state.h
 * @brief PC-control state store: PC state, active command and recent-command
 *        history.
 *
 * Thread-safety: every accessor takes an internal mutex; callers work on
 * by-value snapshots (all structs are small). Updates that target the active
 * command are correlated by request_id so a late writer cannot corrupt a
 * different command.
 *
 * Honesty rules encoded in this store:
 *  - the device has no power-sensing input, so PC power state is only ever
 *    claimed with an explicit source/note (otherwise it stays UNKNOWN);
 *  - power_source/power_note label how any non-UNKNOWN claim was obtained.
 */

#pragma once

#include "pc_cmd.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_STATE_HISTORY 4
#define PC_ID_MAX        40   /* request id buffer incl. NUL (max 32 chars) */
#define PC_DETAIL_MAX    112
#define PC_ERROR_MAX     96

typedef enum {
    PC_PWR_UNKNOWN = 0,   /* default: no power-state signal available */
    PC_PWR_ONLINE,
    PC_PWR_OFFLINE,
    PC_PWR_SLEEPING,
} pc_power_state_t;

typedef struct {
    char request_id[PC_ID_MAX];
    pc_cmd_id_t id;
    pc_cmd_state_t state;
    int64_t created_us;
    int64_t updated_us;
    int64_t deadline_us;      /* 0 = none */
    char detail[PC_DETAIL_MAX];
    char error[PC_ERROR_MAX];
} pc_cmd_slot_t;

typedef struct {
    /* Physical PC state (no power-sensing input: usually UNKNOWN) */
    pc_power_state_t power;
    int64_t power_updated_us;
    char power_source[16];           /* none | hardware */
    char power_note[80];

    /* Commands */
    pc_cmd_slot_t active;                          /* NONE when idle */
    pc_cmd_slot_t history[PC_STATE_HISTORY];       /* newest first */
    int history_count;
} pc_state_snapshot_t;

/** One-time init (creates the mutex; zeroes the store). */
void pc_state_init(void);

/** Full snapshot copy. */
void pc_state_get(pc_state_snapshot_t *out);

/* --- power --- */
void pc_state_set_power(pc_power_state_t p, const char *source, const char *note);

/* --- commands --- */
/* Activates `slot` as the one active command. Returns false when another
 * command is active (caller receives it in active_out when non-NULL). */
bool pc_state_activate(const pc_cmd_slot_t *slot, pc_cmd_slot_t *active_out);

/* Updates the active command only when request_id matches. deadline_us_set
 * > 0 replaces the deadline; 0 keeps it. Returns true when applied. */
bool pc_state_active_update(const char *request_id, pc_cmd_state_t state,
                            const char *detail, const char *error,
                            int64_t deadline_us_set);

/* Moves the active command (matching request_id) to the history ring. */
void pc_state_active_finish(const char *request_id);

/* Finds a command by id: active first, then history (newest first). */
bool pc_state_find(const char *request_id, pc_cmd_slot_t *out);

const char *pc_power_state_str(pc_power_state_t p);

#ifdef __cplusplus
}
#endif
