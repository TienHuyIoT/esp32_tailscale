/**
 * @file pc_config.h
 * @brief Runtime PC-controller configuration (NVS-backed).
 *
 * Everything the user configures at runtime lives here:
 *   - Windows PC tailnet address / service port / HTTP timeout / agent token
 *   - six independent function Enable flags
 *   - GPIO mapping (NC or an allowed pin) + press timings
 *
 * Storage: NVS namespace "pc_ctl", keys "conn" and "hw" (versioned blobs).
 * Kconfig values are only *defaults*: they seed the first boot when NVS is
 * empty. NVS is written ONLY on explicit user saves through the settings
 * API - never while fields are being edited.
 *
 * Validation rules (enforced here, firmware-side, not just in the UI):
 *   - host: 1..63 chars of [A-Za-z0-9.-] (tailnet IP or hostname)
 *   - port: 1..65535; timeout: 1000..30000 ms
 *   - token: 4..95 printable non-space chars; empty = cleared
 *   - GPIO: NC (-1) or a pin from the verified list for THIS device
 *   - PWR_RESET must never share a pin with a PWR_SW-family function
 *     (POWER_ON / WAKE_UP / FORCE_POWER_OFF may share one PWR_SW output)
 */

#pragma once

#include "esp_err.h"
#include "pc_cmd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_CFG_HOST_MAX 64
#define PC_CFG_TOKEN_MAX 96
#define PC_CFG_TIMEOUT_MIN_MS 1000
#define PC_CFG_TIMEOUT_MAX_MS 30000
#define PC_CFG_SHORT_PRESS_MIN 100
#define PC_CFG_SHORT_PRESS_MAX 2000
#define PC_CFG_LONG_PRESS_MIN 1000
#define PC_CFG_LONG_PRESS_MAX 15000

/* GPIO slots: 0=POWER_ON, 1=WAKE_UP, 2=PWR_RESET, 3=FORCE_POWER_OFF */
#define PC_CFG_GPIO_SLOTS 4

typedef struct {
    char     host[PC_CFG_HOST_MAX];
    uint16_t port;
    uint32_t timeout_ms;
    bool     token_set;                 /* token value itself is never exposed */
    uint32_t funcs_mask;                /* bit i = enabled for pc_cmd_id_t i */
    int8_t   gpio[PC_CFG_GPIO_SLOTS];   /* -1 = NC */
    uint16_t short_press_ms;            /* POWER_ON / WAKE_UP / PWR_RESET pulse */
    uint16_t long_press_ms;             /* FORCE_POWER_OFF hold */
} pc_config_snapshot_t;

/** One-time init after nvs_flash_init() (loads NVS or Kconfig defaults). */
void pc_config_init(void);

/** Full copy of the effective configuration. */
void pc_config_get(pc_config_snapshot_t *out);

/** Copy the agent bearer token (internal use only). false = not set. */
bool pc_config_token(char *out, size_t out_len);

/** Lazy patch: validates everything first, then persists once. No partial
 *  writes on validation failure. */
typedef struct {
    bool set_host;    const char *host;
    bool set_port;    int port;
    bool set_timeout; int timeout_ms;
    bool set_funcs;   uint32_t funcs_mask;
    bool set_gpio[PC_CFG_GPIO_SLOTS]; int gpio[PC_CFG_GPIO_SLOTS]; /* -1 = NC */
    bool set_timings; int short_ms; int long_ms;
} pc_config_patch_t;

esp_err_t pc_config_apply(const pc_config_patch_t *patch);

/** Set or clear ("") the agent token; persisted. */
esp_err_t pc_config_set_token(const char *token);

/** GPIO policy helpers (firmware-side enforcement). */
bool pc_cmd_function_enabled(const pc_config_snapshot_t *cfg, pc_cmd_id_t id);
int  pc_cmd_gpio_slot(pc_cmd_id_t id);                 /* -1 = software function */
bool pc_config_gpio_allowed(int pin);
const int *pc_config_allowed_gpios(size_t *count);
