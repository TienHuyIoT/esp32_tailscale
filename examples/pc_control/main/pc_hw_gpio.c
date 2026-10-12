/**
 * @file pc_hw_gpio.c
 * @brief Real GPIO hardware implementation (PWR_SW / RESET_SW pulses).
 *
 * Compiled only with CONFIG_PCCTL_ENABLE_HW_GPIO=y, which must stay off
 * until an electrically verified interface exists between the ESP32 and the
 * motherboard front-panel pins (transistor / optocoupler / isolated relay).
 * Never wire a GPIO straight to PWR_SW# / RESET_SW#.
 *
 * Boot/reset safety: pins come from the runtime Settings UI (NVS) and are
 * driven ONLY on demand inside execute() - nothing is touched at boot, so a
 * reset or reboot cannot actuate anything. An ESP32 reboot mid-pulse simply
 * ends the pulse (the pin returns to its default state). An external pulldown
 * in the interface circuit is still recommended.
 *
 * Assert level: "active HIGH" / "active LOW" is a runtime setting per output
 * (Settings UI, NVS) - the level driven to press the button; idle is the
 * opposite level. Default HIGH suits the typical transistor/optocoupler
 * stage into an active-low PWR_SW#/RESET_SW# - VERIFY the configured level
 * against your circuit before enabling this mode.
 */

#include "sdkconfig.h"

#if CONFIG_PCCTL_ENABLE_HW_GPIO

#include "pc_hw.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "pc_hw_gpio";

/* Legacy fixed levels are gone: assert/idle are derived per pulse from the
 * runtime "active HIGH/LOW" setting (pc_hw_params_t.active_high). */

static esp_err_t gpio_init(void) {
    /* Pins come from the runtime Settings UI (NVS) and are driven only on
     * demand inside execute() - nothing is touched at boot. */
    ESP_LOGW(TAG, "GPIO hardware mode compiled in - outputs are driven only "
                  "on demand from the runtime pin mapping; verify the "
                  "interface circuit before enabling FULL mode");
    return ESP_OK;
}

static uint32_t gpio_duration(pc_cmd_id_t id, const pc_hw_params_t *params) {
    uint32_t pulse = (params && params->pulse_ms) ? params->pulse_ms : 300;
    if (pulse < 100) pulse = 100;
    if (pulse > 2000) pulse = 2000;
    switch (id) {
    case PC_CMD_POWER_ON:
    case PC_CMD_WAKE_UP:
    case PC_CMD_PWR_RESET:
        return pulse;
    case PC_CMD_FORCE_POWER_OFF: {
        uint32_t hold = (params && params->hold_ms) ? params->hold_ms
                                                    : (uint32_t)CONFIG_PCCTL_FORCE_OFF_HOLD_MS;
        if (hold < 1000) hold = 1000;
        if (hold > 15000) hold = 15000;
        return hold;
    }
    default:
        return 300;
    }
}

static esp_err_t pulse(int pin, uint32_t ms, bool active_high) {
    if (pin < 0) return ESP_ERR_INVALID_STATE;
    const int assert_level = active_high ? 1 : 0;
    const int idle_level   = active_high ? 0 : 1;
    /* Pre-load the output latch with the IDLE level BEFORE enabling the
     * output driver: enabling first would briefly drive the latch's
     * reset-default value (0) - for active-LOW wiring 0 is the assert
     * level, i.e. a false-press glitch at every pulse start. With the
     * latch pre-set, the driver enables straight into idle. */
    gpio_set_level(pin, idle_level);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, assert_level);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, idle_level);
    return ESP_OK;
}

static esp_err_t gpio_execute(pc_cmd_id_t id, const pc_hw_params_t *params,
                              char *detail, size_t detail_len) {
    if (!params || params->gpio_pin < 0) {
        snprintf(detail, detail_len, "no GPIO configured for this function");
        return ESP_ERR_INVALID_STATE;
    }
    int pin = params->gpio_pin;
    const bool active_high = params->active_high;
    const char *lvl = active_high ? "HIGH" : "LOW";
    uint32_t dur = gpio_duration(id, params);
    esp_err_t err;
    switch (id) {
    case PC_CMD_POWER_ON:
        err = pulse(pin, dur, active_high);
        snprintf(detail, detail_len,
                 "PWR_SW short press %lu ms on GPIO%d (%s) issued (outcome not verified)",
                 (unsigned long)dur, pin, lvl);
        break;
    case PC_CMD_WAKE_UP:
        err = pulse(pin, dur, active_high);
        snprintf(detail, detail_len, "PWR_SW wake press %lu ms on GPIO%d (%s) issued",
                 (unsigned long)dur, pin, lvl);
        break;
    case PC_CMD_PWR_RESET:
        err = pulse(pin, dur, active_high);
        snprintf(detail, detail_len, "RESET_SW press %lu ms on GPIO%d (%s) issued",
                 (unsigned long)dur, pin, lvl);
        break;
    case PC_CMD_FORCE_POWER_OFF:
        err = pulse(pin, dur, active_high);
        snprintf(detail, detail_len, "PWR_SW held %lu ms on GPIO%d (%s, force off)",
                 (unsigned long)dur, pin, lvl);
        break;
    default:
        err = ESP_ERR_NOT_SUPPORTED;
        snprintf(detail, detail_len, "not supported");
        break;
    }
    if (err != ESP_OK) {
        snprintf(detail, detail_len, "GPIO action failed: %s", esp_err_to_name(err));
    }
    return err;
}

const pc_hw_ops_t pc_hw_gpio_ops = {
    .name = "gpio",
    .init = gpio_init,
    .execute = gpio_execute,
    .duration_ms = gpio_duration,
};

#else  /* !CONFIG_PCCTL_ENABLE_HW_GPIO */

/* Keep the translation unit non-empty when the feature is compiled out. */
typedef int pc_hw_gpio_disabled_t;

#endif /* CONFIG_PCCTL_ENABLE_HW_GPIO */
