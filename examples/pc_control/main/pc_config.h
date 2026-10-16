/**
 * @file pc_config.h
 * @brief Runtime PC-controller configuration (NVS-backed).
 *
 * What the user configures at runtime lives here:
 *   - four independent function Enable flags (POWER_ON / WAKE_UP /
 *     PWR_RESET / FORCE_POWER_OFF)
 *   - GPIO mapping (NC or an allowed pin) + assert level + press timings
 *
 * Storage: NVS namespace "pc_ctl", key "hw" (versioned blob). "hw" v2 adds
 * a per-slot assert level; v1 blobs (byte prefix of v2) are accepted and
 * migrated in RAM - levels default to HIGH (= v1 behavior) and the v2
 * layout is persisted on the next explicit save. Kconfig values are only
 * *defaults*: they seed the first boot when NVS is empty. NVS is written
 * ONLY on explicit user saves through the settings API - never while
 * fields are being edited.
 *
 * Validation rules (enforced here, firmware-side, not just in the UI):
 *   - GPIO: NC (-1) or a pin from the verified list for THIS device
 *   - assert level: PC_CFG_LEVEL_HIGH (default) or PC_CFG_LEVEL_LOW;
 *     slots sharing a pin must use the same level
 *   - PWR_RESET must never share a pin with a PWR_SW-family function
 *     (POWER_ON / WAKE_UP / FORCE_POWER_OFF may share one PWR_SW output)
 *
 * The API token and the admin login live in pc_auth.c (same NVS namespace).
 */

#pragma once

#include "esp_err.h"
#include "pc_cmd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_CFG_SHORT_PRESS_MIN 100
#define PC_CFG_SHORT_PRESS_MAX 2000
#define PC_CFG_LONG_PRESS_MIN 1000
#define PC_CFG_LONG_PRESS_MAX 15000

/* GPIO slots: 0=POWER_ON, 1=WAKE_UP, 2=PWR_RESET, 3=FORCE_POWER_OFF */
#define PC_CFG_GPIO_SLOTS 4

/* Assert level per slot: the level driven to "press" the front-panel input
 * (idle = the opposite level). HIGH is the default and matches the classic
 * transistor/optocoupler stage into an active-low PWR_SW#/RESET_SW#. */
#define PC_CFG_LEVEL_LOW  0
#define PC_CFG_LEVEL_HIGH 1

typedef struct {
    uint32_t funcs_mask;                /* bit i = enabled for pc_cmd_id_t i */
    int8_t   gpio[PC_CFG_GPIO_SLOTS];   /* -1 = NC */
    uint8_t  level[PC_CFG_GPIO_SLOTS];  /* PC_CFG_LEVEL_HIGH / _LOW */
    uint16_t short_press_ms;            /* POWER_ON / WAKE_UP / PWR_RESET pulse */
    uint16_t long_press_ms;             /* FORCE_POWER_OFF hold */
} pc_config_snapshot_t;

/** One-time init after nvs_flash_init() (loads NVS or Kconfig defaults). */
void pc_config_init(void);

/** Full copy of the effective configuration. */
void pc_config_get(pc_config_snapshot_t *out);

/** Lazy patch: validates everything first, then persists once. No partial
 *  writes on validation failure. */
typedef struct {
    bool set_funcs;   uint32_t funcs_mask;
    bool set_gpio[PC_CFG_GPIO_SLOTS]; int gpio[PC_CFG_GPIO_SLOTS]; /* -1 = NC */
    bool set_level[PC_CFG_GPIO_SLOTS]; int level[PC_CFG_GPIO_SLOTS]; /* 0/1 */
    bool set_timings; int short_ms; int long_ms;
} pc_config_patch_t;

esp_err_t pc_config_apply(const pc_config_patch_t *patch);

/** GPIO policy helpers (firmware-side enforcement). */
bool pc_cmd_function_enabled(const pc_config_snapshot_t *cfg, pc_cmd_id_t id);
int  pc_cmd_gpio_slot(pc_cmd_id_t id);                 /* -1 = software function */
bool pc_config_gpio_allowed(int pin);
const int *pc_config_allowed_gpios(size_t *count);
