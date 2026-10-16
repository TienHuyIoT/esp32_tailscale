/**
 * @file pc_hw.h
 * @brief Hardware layer for POWER_ON / WAKE_UP / PWR_RESET / FORCE_POWER_OFF:
 *        real GPIO pulses on the motherboard front-panel inputs.
 *
 * There is exactly one implementation (pc_hw.c) - no simulation mode. Pins,
 * active level and pulse timings come from the runtime Settings UI (NVS) and
 * are driven ONLY on demand, so a reset or reboot cannot actuate anything.
 *
 * Never wire a GPIO straight to PWR_SW# / RESET_SW#: use a transistor /
 * optocoupler / isolated relay stage.
 */

#pragma once

#include "esp_err.h"
#include "pc_cmd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio_pin;       /* runtime-selected output (>= 0; caller validates) */
    bool active_high;   /* true = HIGH asserts; false = LOW (idle = opposite) */
    uint32_t pulse_ms;  /* short-press duration (POWER_ON/WAKE_UP/PWR_RESET) */
    uint32_t hold_ms;   /* FORCE_POWER_OFF hold override (0 = default long) */
} pc_hw_params_t;

/** Configure the outputs; safe to call once at startup. A non-OK result makes
 *  the controller report every command as unavailable. */
esp_err_t pc_hw_init(void);

/** Execute synchronously (blocks for the action duration). Called from the
 *  pc_ctl task only. Fills `detail` with an honest result line. */
esp_err_t pc_hw_execute(pc_cmd_id_t id, const pc_hw_params_t *params,
                        char *detail, size_t detail_len);

/** Action duration in ms (used for deadlines/progress display). */
uint32_t pc_hw_duration_ms(pc_cmd_id_t id, const pc_hw_params_t *params);

#ifdef __cplusplus
}
#endif
