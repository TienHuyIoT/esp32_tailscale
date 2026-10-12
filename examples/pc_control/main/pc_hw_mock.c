/**
 * @file pc_hw_mock.c
 * @brief Mock hardware implementation: never touches GPIO, never triggers a
 *        physical action. Simulates the four hardware commands with honest,
 *        clearly-labelled results.
 */

#include "pc_hw.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "pc_hw_mock";

#define MOCK_DEFAULT_PULSE_MS 300
#define FORCE_HOLD_MIN_MS     1000
#define FORCE_HOLD_MAX_MS     15000

static uint32_t clamp_hold(uint32_t hold) {
    if (hold < FORCE_HOLD_MIN_MS) hold = FORCE_HOLD_MIN_MS;
    if (hold > FORCE_HOLD_MAX_MS) hold = FORCE_HOLD_MAX_MS;
    return hold;
}

static uint32_t mock_duration(pc_cmd_id_t id, const pc_hw_params_t *params) {
    switch (id) {
    case PC_CMD_POWER_ON:
    case PC_CMD_WAKE_UP:
    case PC_CMD_PWR_RESET:
        return (params && params->pulse_ms) ? params->pulse_ms : MOCK_DEFAULT_PULSE_MS;
    case PC_CMD_FORCE_POWER_OFF: {
        uint32_t hold = (params && params->hold_ms) ? params->hold_ms
                                                    : (uint32_t)CONFIG_PCCTL_FORCE_OFF_HOLD_MS;
        return clamp_hold(hold);
    }
    default:
        return MOCK_DEFAULT_PULSE_MS;
    }
}

static esp_err_t mock_init(void) {
    ESP_LOGI(TAG, "mock hardware active - no GPIO is touched in this mode");
    return ESP_OK;
}

static esp_err_t mock_execute(pc_cmd_id_t id, const pc_hw_params_t *params,
                              char *detail, size_t detail_len) {
    const pc_cmd_def_t *def = pc_cmd_def(id);
    uint32_t dur = mock_duration(id, params);

    ESP_LOGI(TAG, "[MOCK] %s: simulating %lu ms action (no physical output)",
             def ? def->name : "?", (unsigned long)dur);
    vTaskDelay(pdMS_TO_TICKS(dur));

    const char *what;
    switch (id) {
    case PC_CMD_POWER_ON:        what = "simulated PWR_SW short press"; break;
    case PC_CMD_WAKE_UP:         what = "simulated PWR_SW wake press"; break;
    case PC_CMD_PWR_RESET:       what = "simulated RESET_SW press"; break;
    case PC_CMD_FORCE_POWER_OFF: what = "simulated long PWR_SW hold"; break;
    default:                     what = "simulated action"; break;
    }
    snprintf(detail, detail_len,
             "%s on GPIO%d (%lu ms) - MOCK mode, nothing physical happened",
             what, params ? params->gpio_pin : -1, (unsigned long)dur);
    return ESP_OK;
}

const pc_hw_ops_t pc_hw_mock_ops = {
    .name = "mock",
    .init = mock_init,
    .execute = mock_execute,
    .duration_ms = mock_duration,
};
