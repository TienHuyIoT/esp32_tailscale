/**
 * @file pc_relay.h
 * @brief Relay Manager: configurable relay channels driving real GPIO outputs.
 *
 * Each channel has a stable id (its index), a user-configurable display name,
 * an Enable flag, a GPIO (NC = -1), an active level reusing the PC-control
 * PC_CFG_LEVEL_HIGH/LOW convention (the level driven to energize the relay)
 * and a fixed safe startup policy (OFF - energized loads are never restored
 * after a reboot).
 *
 * Storage: NVS namespace "pc_ctl", key "relays" (versioned blob), written
 * ONLY on explicit user saves. The commanded ON/OFF state is RAM-only and
 * resets to OFF on boot and on every configuration change.
 *
 * Safety rules (enforced here, firmware-side - not just in the UI):
 *  - every configured pin is driven to its idle (OFF) level at boot and on
 *    every configuration change - a configuration change can never pulse;
 *  - relay pins must come from the board GPIO allowlist
 *    (pc_config_gpio_allowed), be unique across channels and must not
 *    overlap the PC-control function pins (pc_config.c re-checks too);
 *  - ON/OFF commands are rejected for disabled channels and GPIO = NC;
 *  - the API reports only the COMMANDED logical state: there is no relay
 *    contact feedback, so physical switching is never claimed.
 *
 * Logical vs electrical: the API/UI use logical ON/OFF. ON maps to the
 * configured active level; OFF maps to the opposite (idle) level. The relay
 * logic is independent of whether the switched load is AC or DC - never
 * infer the load type or rating from this configuration.
 */

#pragma once

#include "esp_err.h"
#include "pc_config.h"   /* PC_CFG_LEVEL_HIGH/_LOW - shared level convention */
#include "sdkconfig.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channel name: visible text, at most PC_RELAY_NAME_MAX bytes (excl. NUL).
 * Empty names are allowed; the UI then shows a default ("Relay N"). */
#define PC_RELAY_NAME_MAX 32

/* Startup policy: only OFF is supported. Energized loads are deliberately
 * NOT restored after a reboot; other values are rejected at validation. */
#define PC_RELAY_STARTUP_OFF 0

typedef struct {
    char    name[PC_RELAY_NAME_MAX + 1]; /* display name (may be empty) */
    bool    enabled;
    int8_t  gpio;            /* -1 = NC (not configured) */
    uint8_t active_level;    /* PC_CFG_LEVEL_HIGH / PC_CFG_LEVEL_LOW */
    uint8_t startup_policy;  /* PC_RELAY_STARTUP_OFF only */
    bool    state;           /* commanded logical state (RAM only) */
} pc_relay_channel_t;

typedef struct {
    uint8_t channel_count;   /* active channels (0..max_channels) */
    uint8_t max_channels;    /* firmware limit (CONFIG_PCCTL_RELAY_MAX_CHANNELS) */
    pc_relay_channel_t channels[CONFIG_PCCTL_RELAY_MAX_CHANNELS];
} pc_relay_snapshot_t;

/** One-time init after nvs_flash_init() and pc_config_init() (the NVS load
 *  cross-checks the PC-control pin ownership). Loads + validates the stored
 *  relay configuration and drives every configured pin to its idle (OFF)
 *  level before control is possible. */
void pc_relay_init(void);

/** Full copy of the relay configuration and commanded states. */
void pc_relay_get(pc_relay_snapshot_t *out);

/** True when the pin is configured on any relay channel (reserved for the
 *  relay feature) - even when the channel is disabled or above the active
 *  channel count. Safe to call before pc_relay_init(). */
bool pc_relay_gpio_in_use(int pin);

/** Availability of one channel for ON/OFF commands. `reason` (optional)
 *  receives a static string when unavailable. */
bool pc_relay_channel_available(const pc_relay_snapshot_t *snap, uint8_t id,
                                const char **reason);

/** Set the commanded logical state of one channel. Idempotent: setting the
 *  state it already has is a no-op success, so retries can never reverse
 *  the intended state. Fails with ESP_ERR_INVALID_ARG for unknown ids and
 *  ESP_ERR_INVALID_STATE for disabled / GPIO = NC channels (callers should
 *  pre-check with pc_relay_channel_available for distinct API errors);
 *  underlying GPIO driver errors are propagated. */
esp_err_t pc_relay_set_state(uint8_t id, bool on);

/* --- configuration (device Settings UI / HTTP API) --- */

typedef struct {
    bool set_name;    char name[PC_RELAY_NAME_MAX + 1];
    bool set_enabled; bool enabled;
    bool set_gpio;    int  gpio;     /* -1 = NC */
    bool set_level;   int  level;    /* PC_CFG_LEVEL_HIGH / _LOW */
    bool set_startup; int  startup;  /* PC_RELAY_STARTUP_OFF only */
} pc_relay_channel_patch_t;

typedef struct {
    bool set_channel_count; uint8_t channel_count;
    bool set_channels[CONFIG_PCCTL_RELAY_MAX_CHANNELS];
    pc_relay_channel_patch_t channels[CONFIG_PCCTL_RELAY_MAX_CHANNELS];
} pc_relay_config_patch_t;

/** Validate the whole patched configuration first, then persist it in ONE
 *  NVS write and apply it: every channel ends up logically OFF and every
 *  pin (old and new) driven to an idle level - a change can never pulse.
 *  Returns:
 *   ESP_ERR_INVALID_ARG   invalid field value / invalid GPIO / bad name
 *   ESP_ERR_INVALID_STATE GPIO conflict (duplicate pin or PC-control pin)
 *   ESP_ERR_NOT_ALLOWED   unsafe channel-count reduction (a channel being
 *                         dropped still has a GPIO mapping or is enabled)
 *   other                 NVS persist error on the new blob (nothing is
 *                         applied on failure) */
esp_err_t pc_relay_config_apply(const pc_relay_config_patch_t *patch);

#ifdef __cplusplus
}
#endif
