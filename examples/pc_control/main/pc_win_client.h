/**
 * @file pc_win_client.h
 * @brief HTTP client for the Windows PC agent (tools/pc_agent), spoken over
 *        a MicroLink TCP socket through the WireGuard tunnel.
 *
 * Single-threaded usage contract: call these functions only from the pc_ctl
 * task (the exchange uses static buffers and blocking sockets).
 */

#pragma once

#include "esp_err.h"
#include "microlink.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ok;               /* transport + HTTP + agent ok:true */
    int http_status;       /* -1 = no HTTP response */
    char error[96];        /* human-readable failure summary */
    bool agent_ok;         /* agent's own "ok" field */
    bool dry_run;          /* agent in dry-run (no real execution) */
    bool pending;          /* agent has a pending shutdown/restart */
    char operation[16];    /* pending operation ("shutdown"/"restart"/"") */
    int64_t remaining_s;   /* pending remaining seconds (-1 unknown) */
    bool cancelled;        /* cancel outcome */
    char reason[64];       /* cancel reason / misc detail from agent */
} pc_win_result_t;

esp_err_t pc_win_client_setup(microlink_t *ml);

/** Use a short connect timeout while the agent is known to be offline
 *  (set by the controller after a failed poll; cleared on success). Keeps
 *  queued commands from waiting behind multi-second connect attempts. */
esp_err_t pc_win_client_set_fast_fail(bool fast);

/** Drop the cached resolved address (call when the configured host changes
 *  or a connection test wants a fresh resolution). */
void pc_win_client_invalidate(void);

bool pc_win_client_get_status(pc_win_result_t *out);
bool pc_win_client_post_shutdown(uint32_t delay_s, pc_win_result_t *out);
bool pc_win_client_post_restart(uint32_t delay_s, pc_win_result_t *out);
bool pc_win_client_post_cancel(pc_win_result_t *out);

#ifdef __cplusplus
}
#endif
