/**
 * @file pc_hw.h
 * @brief Hardware abstraction for POWER_ON / WAKE_UP / PWR_RESET /
 *        FORCE_POWER_OFF.
 *
 * The active implementation is selected at build time:
 *  - pc_hw_mock  : default; simulates every action, touches no GPIO;
 *  - pc_hw_gpio  : real pulses, compiled only with
 *                  CONFIG_PCCTL_ENABLE_HW_GPIO=y (off until an
 *                  electrically verified interface exists).
 *
 * SHUTDOWN / RESTART are never routed through this interface (they are
 * Windows operations and go to the agent).
 */

#pragma once

#include "esp_err.h"
#include "pc_cmd.h"
#include "sdkconfig.h"
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

typedef struct {
    const char *name;   /* "mock" | "gpio" */
    /* Configure outputs; safe to call once at startup. */
    esp_err_t (*init)(void);
    /* Execute synchronously (blocks for the action duration). Called from
     * the pc_ctl task only. Fills `detail` with an honest result line. */
    esp_err_t (*execute)(pc_cmd_id_t id, const pc_hw_params_t *params,
                         char *detail, size_t detail_len);
    /* Action duration in ms (used for deadlines/progress display). */
    uint32_t (*duration_ms)(pc_cmd_id_t id, const pc_hw_params_t *params);
} pc_hw_ops_t;

extern const pc_hw_ops_t pc_hw_mock_ops;

#if CONFIG_PCCTL_ENABLE_HW_GPIO
extern const pc_hw_ops_t pc_hw_gpio_ops;
#endif

#ifdef __cplusplus
}
#endif
