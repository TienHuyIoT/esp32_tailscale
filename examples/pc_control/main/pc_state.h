/**
 * @file pc_state.h
 * @brief PC-control state store: service reachability, PC state, active
 *        command and recent-command history.
 *
 * Thread-safety: every accessor takes an internal mutex; callers work on
 * by-value snapshots (all structs are small). Updates that target the active
 * command are correlated by request_id so a late writer cannot corrupt a
 * different command.
 *
 * Honesty rules encoded in this store:
 *  - service reachability is tracked separately from physical PC state;
 *  - an unreachable service never implies "PC is off" (power goes to
 *    UNKNOWN with an explanatory source/note instead);
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
    PC_SVC_UNKNOWN = 0,   /* never checked, or cannot check (tailnet down) */
    PC_SVC_ONLINE,
    PC_SVC_OFFLINE,
} pc_service_state_t;

typedef enum {
    PC_PWR_UNKNOWN = 0,   /* default: no power-state signal available */
    PC_PWR_ONLINE,
    PC_PWR_OFFLINE,
    PC_PWR_SLEEPING,
    PC_PWR_SHUTTING_DOWN,
    PC_PWR_RESTARTING,
} pc_power_state_t;

typedef struct {
    char request_id[PC_ID_MAX];
    pc_cmd_id_t id;
    pc_cmd_state_t state;
    int64_t created_us;
    int64_t updated_us;
    int64_t deadline_us;      /* 0 = none */
    bool simulated;           /* mock firmware mode or dry-run agent */
    char detail[PC_DETAIL_MAX];
    char error[PC_ERROR_MAX];
} pc_cmd_slot_t;

typedef struct {
    /* Windows agent (service) */
    pc_service_state_t service;
    int64_t service_last_ok_us;      /* 0 = never */
    int64_t service_last_check_us;   /* 0 = never */
    bool service_agent_dry_run;
    char service_error[PC_ERROR_MAX];

    /* Physical PC state (kept separate from service reachability) */
    pc_power_state_t power;
    int64_t power_updated_us;
    char power_source[16];           /* none | agent | agent_lost | hardware */
    char power_note[80];

    /* Last explicit connection test (from the device Settings UI) */
    bool     test_valid;
    int64_t  test_at_us;
    bool     test_reachable;
    bool     test_authenticated;
    int      test_http_status;
    char     test_error[PC_ERROR_MAX];

    /* Commands */
    pc_cmd_slot_t active;                          /* NONE when idle */
    pc_cmd_slot_t history[PC_STATE_HISTORY];       /* newest first */
    int history_count;
} pc_state_snapshot_t;

/** One-time init (creates the mutex; zeroes the store). */
void pc_state_init(void);

/** Full snapshot copy. */
void pc_state_get(pc_state_snapshot_t *out);

/* --- service --- */
void pc_state_set_service_unknown(const char *error);
void pc_state_set_service(pc_service_state_t st, bool agent_dry_run, const char *error);

/* --- power --- */
void pc_state_set_power(pc_power_state_t p, const char *source, const char *note);

/* --- connection test (Settings UI) --- */
void pc_state_set_test(bool reachable, bool authenticated, int http_status,
                       const char *error);

/* --- commands --- */
/* Activates `slot` as the one active command. Returns false when another
 * command is active (caller receives it in active_out when non-NULL). */
bool pc_state_activate(const pc_cmd_slot_t *slot, pc_cmd_slot_t *active_out);

/* Updates the active command only when request_id matches. deadline_us_set
 * > 0 replaces the deadline; 0 keeps it. Returns true when applied. */
bool pc_state_active_update(const char *request_id, pc_cmd_state_t state,
                            const char *detail, const char *error,
                            int64_t deadline_us_set);

bool pc_state_active_set_simulated(const char *request_id, bool simulated);

/* Moves the active command (matching request_id) to the history ring. */
void pc_state_active_finish(const char *request_id);

/* Finds a command by id: active first, then history (newest first). */
bool pc_state_find(const char *request_id, pc_cmd_slot_t *out);

const char *pc_power_state_str(pc_power_state_t p);
const char *pc_service_state_str(pc_service_state_t s);

#ifdef __cplusplus
}
#endif
