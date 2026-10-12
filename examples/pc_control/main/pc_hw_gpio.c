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

/* Assert level driven on the output pin. HIGH suits the typical
 * transistor/optocoupler interface to an active-low front-panel pin -
 * VERIFY against your circuit before enabling this mode. */
#define ASSERT_LEVEL 1
#define IDLE_LEVEL   0

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

static esp_err_t pulse(int pin, uint32_t ms) {
    if (pin < 0) return ESP_ERR_INVALID_STATE;
    /* Configure lazily and start from the idle level so a reboot cannot
     * leave the pin configured but floating at the assert level. */
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, IDLE_LEVEL);
    gpio_set_level(pin, ASSERT_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, IDLE_LEVEL);
    return ESP_OK;
}

static esp_err_t gpio_execute(pc_cmd_id_t id, const pc_hw_params_t *params,
                              char *detail, size_t detail_len) {
    if (!params || params->gpio_pin < 0) {
        snprintf(detail, detail_len, "no GPIO configured for this function");
        return ESP_ERR_INVALID_STATE;
    }
    int pin = params->gpio_pin;
    uint32_t dur = gpio_duration(id, params);
    esp_err_t err;
    switch (id) {
    case PC_CMD_POWER_ON:
        err = pulse(pin, dur);
        snprintf(detail, detail_len,
                 "PWR_SW short press %lu ms on GPIO%d issued (outcome not verified)",
                 (unsigned long)dur, pin);
        break;
    case PC_CMD_WAKE_UP:
        err = pulse(pin, dur);
        snprintf(detail, detail_len, "PWR_SW wake press %lu ms on GPIO%d issued",
                 (unsigned long)dur, pin);
        break;
    case PC_CMD_PWR_RESET:
        err = pulse(pin, dur);
        snprintf(detail, detail_len, "RESET_SW press %lu ms on GPIO%d issued",
                 (unsigned long)dur, pin);
        break;
    case PC_CMD_FORCE_POWER_OFF:
        err = pulse(pin, dur);
        snprintf(detail, detail_len, "PWR_SW held %lu ms on GPIO%d (force off)",
                 (unsigned long)dur, pin);
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
