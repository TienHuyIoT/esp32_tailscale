#include "pc_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "pc_config";

#define NVS_NS        "pc_ctl"
#define NVS_KEY_HW    "hw"
#define HW_VERSION    2   /* v2 adds per-slot active levels (v1 blobs migrate) */
#define HW_VERSION_V1 1

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint32_t funcs_mask;
    int8_t   gpio[PC_CFG_GPIO_SLOTS];
    uint16_t short_press_ms;
    uint16_t long_press_ms;
    uint8_t  level[PC_CFG_GPIO_SLOTS];   /* v2: PC_CFG_LEVEL_HIGH / _LOW */
} pc_hw_blob_t;

/* v1 (no level[]) is a byte-prefix of v2: kept only to recognize and
 * migrate an old blob; v1 always behaved as active-HIGH. */
typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint32_t funcs_mask;
    int8_t   gpio[PC_CFG_GPIO_SLOTS];
    uint16_t short_press_ms;
    uint16_t long_press_ms;
} pc_hw_blob_v1_t;

/* Verified usable output pins for THIS device (see docs/PC_CONTROLLER.md
 * "GPIO policy"): ESP32-S3 rev v0.2, 8 MB octal PSRAM, 16 MB flash,
 * console on UART0 (external USB-UART bridge - COM9).
 * Excluded: 0/3/45/46 strapping, 19/20 USB-JTAG, 26-32 SPI flash,
 * 33-37 octal PSRAM, 38-42 JTAG, 43/44 UART0 console.
 * Adjust in code for a different board. */
static const int s_allowed_gpios[] = {
    1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 47, 48
};

static pc_hw_blob_t s_hw;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static uint32_t default_funcs_mask(void) {
    /* Every remaining function drives a motherboard input, so nothing is
     * enabled until the user enables it AND maps a GPIO (Settings UI). */
    return 0;
}

static void load_defaults(void) {
    memset(&s_hw, 0, sizeof(s_hw));
    s_hw.version = HW_VERSION;
    s_hw.funcs_mask = default_funcs_mask();
    for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) {
        s_hw.gpio[i] = -1;
        s_hw.level[i] = PC_CFG_LEVEL_HIGH;   /* preserve the previous behavior */
    }
    s_hw.short_press_ms = 300;
    s_hw.long_press_ms = CONFIG_PCCTL_FORCE_OFF_HOLD_MS;
}

static void lock(void) { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static esp_err_t persist_blob(const char *key, const void *blob, size_t len) {
    if (!s_nvs_ok) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_blob(s_nvs, key, blob, len);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

void pc_config_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    load_defaults();

    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace '%s' open failed (%s) - using Kconfig defaults",
                 NVS_NS, esp_err_to_name(err));
        return;
    }
    s_nvs_ok = true;

    pc_hw_blob_t hw;
    size_t len = sizeof(hw);
    esp_err_t herr = nvs_get_blob(s_nvs, NVS_KEY_HW, &hw, &len);
    bool v2_valid = (herr == ESP_OK) && (len == sizeof(hw)) &&
                    (hw.version == HW_VERSION);
    bool v1_valid = (herr == ESP_OK) && (len == sizeof(pc_hw_blob_v1_t)) &&
                    (hw.version == HW_VERSION_V1);
    bool levels_ok = true;
    if (v2_valid) {
        for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) {
            if (hw.level[i] != PC_CFG_LEVEL_HIGH &&
                hw.level[i] != PC_CFG_LEVEL_LOW) {
                levels_ok = false;
                break;
            }
        }
    }
    if ((v2_valid || v1_valid) && levels_ok &&
        hw.short_press_ms >= PC_CFG_SHORT_PRESS_MIN &&
        hw.long_press_ms >= PC_CFG_LONG_PRESS_MIN) {
        if (v1_valid) {
            /* v1 -> v2: v1 is a byte-prefix of v2 (no level[]), so the
             * pins/timings above are already valid; default every level
             * to HIGH (= the v1 hardcoded behavior). This migration is
             * RAM-only - nothing is written back until the next explicit
             * save (the usual "no NVS writes without a user action" rule
             * still holds). */
            for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) {
                hw.level[i] = PC_CFG_LEVEL_HIGH;
            }
            hw.version = HW_VERSION;
            ESP_LOGI(TAG, "hardware config v1 -> v2 migrated in RAM "
                          "(active levels default to HIGH)");
        }
        /* A mask written by an older firmware can carry bits for commands
         * that no longer exist (e.g. the removed SHUTDOWN/RESTART). Mask
         * them off: the settings API validates the mask against the known
         * command set, so a stale bit would make every save fail. */
        hw.funcs_mask &= (1u << PC_CMD_COUNT) - 1u;
        s_hw = hw;
        ESP_LOGI(TAG, "hardware config from NVS: funcs=0x%02lx gpio=[%d,%d,%d,%d] "
                      "lvl=[%d,%d,%d,%d] short=%u long=%u",
                 (unsigned long)s_hw.funcs_mask, s_hw.gpio[0], s_hw.gpio[1],
                 s_hw.gpio[2], s_hw.gpio[3],
                 s_hw.level[0], s_hw.level[1], s_hw.level[2], s_hw.level[3],
                 (unsigned)s_hw.short_press_ms, (unsigned)s_hw.long_press_ms);
    } else {
        ESP_LOGI(TAG, "no valid NVS hardware config - plan defaults apply");
    }
    ESP_LOGI(TAG, "effective hardware config: funcs=0x%02lx",
             (unsigned long)s_hw.funcs_mask);
}

void pc_config_get(pc_config_snapshot_t *out) {
    if (!out) return;
    lock();
    memset(out, 0, sizeof(*out));
    out->funcs_mask = s_hw.funcs_mask;
    for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) {
        out->gpio[i] = s_hw.gpio[i];
        out->level[i] = s_hw.level[i];
    }
    out->short_press_ms = s_hw.short_press_ms;
    out->long_press_ms = s_hw.long_press_ms;
    unlock();
}

static bool gpio_conflicts(const int8_t pins[PC_CFG_GPIO_SLOTS],
                           const uint8_t levels[PC_CFG_GPIO_SLOTS]) {
    /* RESET_SW must be a different output than any PWR_SW-family pin
     * (POWER_ON / WAKE_UP / FORCE_POWER_OFF may share one PWR_SW line). */
    if (pins[2] >= 0) {
        for (int s = 0; s < PC_CFG_GPIO_SLOTS; s++) {
            if (s != 2 && pins[s] == pins[2]) return true;
        }
    }
    /* Slots sharing one physical output must use the same active level:
     * the level belongs to the interface circuit, not to the function.
     * Mixing them would toggle the line's idle state between functions. */
    for (int a = 0; a < PC_CFG_GPIO_SLOTS; a++) {
        if (pins[a] < 0) continue;
        for (int b = a + 1; b < PC_CFG_GPIO_SLOTS; b++) {
            if (pins[b] == pins[a] && levels[a] != levels[b]) return true;
        }
    }
    return false;
}

esp_err_t pc_config_apply(const pc_config_patch_t *patch) {
    if (!patch) return ESP_ERR_INVALID_ARG;

    pc_hw_blob_t hw;
    lock();
    hw = s_hw;
    unlock();

    if (patch->set_funcs) {
        uint32_t known = (1u << PC_CMD_COUNT) - 1u;
        if (patch->funcs_mask & ~known) return ESP_ERR_INVALID_ARG;
        hw.funcs_mask = patch->funcs_mask;
    }
    for (int s = 0; s < PC_CFG_GPIO_SLOTS; s++) {
        if (!patch->set_gpio[s]) continue;
        int pin = patch->gpio[s];
        if (pin != -1 && !pc_config_gpio_allowed(pin)) return ESP_ERR_INVALID_ARG;
        hw.gpio[s] = (int8_t)pin;
    }
    for (int s = 0; s < PC_CFG_GPIO_SLOTS; s++) {
        if (!patch->set_level[s]) continue;
        int lv = patch->level[s];
        if (lv != PC_CFG_LEVEL_LOW && lv != PC_CFG_LEVEL_HIGH) {
            return ESP_ERR_INVALID_ARG;
        }
        hw.level[s] = (uint8_t)lv;
    }
    if (gpio_conflicts(hw.gpio, hw.level)) return ESP_ERR_INVALID_STATE;
    if (patch->set_timings) {
        if (patch->short_ms < PC_CFG_SHORT_PRESS_MIN ||
            patch->short_ms > PC_CFG_SHORT_PRESS_MAX ||
            patch->long_ms < PC_CFG_LONG_PRESS_MIN ||
            patch->long_ms > PC_CFG_LONG_PRESS_MAX) return ESP_ERR_INVALID_ARG;
        hw.short_press_ms = (uint16_t)patch->short_ms;
        hw.long_press_ms = (uint16_t)patch->long_ms;
    }

    /* Persist first; apply to RAM only after the blob is stored, so a failed
     * write never leaves a partially-applied configuration. */
    esp_err_t err = persist_blob(NVS_KEY_HW, &hw, sizeof(hw));
    if (err != ESP_OK) return err;

    lock();
    s_hw = hw;
    unlock();
    ESP_LOGI(TAG, "config applied: funcs=0x%02lx gpio=[%d,%d,%d,%d] "
                  "lvl=[%d,%d,%d,%d]",
             (unsigned long)hw.funcs_mask,
             hw.gpio[0], hw.gpio[1], hw.gpio[2], hw.gpio[3],
             hw.level[0], hw.level[1], hw.level[2], hw.level[3]);
    return ESP_OK;
}

bool pc_config_gpio_allowed(int pin) {
    for (size_t i = 0; i < sizeof(s_allowed_gpios) / sizeof(s_allowed_gpios[0]); i++) {
        if (s_allowed_gpios[i] == pin) return true;
    }
    return false;
}

const int *pc_config_allowed_gpios(size_t *count) {
    if (count) *count = sizeof(s_allowed_gpios) / sizeof(s_allowed_gpios[0]);
    return s_allowed_gpios;
}

bool pc_cmd_function_enabled(const pc_config_snapshot_t *cfg, pc_cmd_id_t id) {
    if (!cfg || id < 0 || id >= PC_CMD_COUNT) return false;
    return (cfg->funcs_mask & (1u << id)) != 0;
}

int pc_cmd_gpio_slot(pc_cmd_id_t id) {
    switch (id) {
    case PC_CMD_POWER_ON:        return 0;
    case PC_CMD_WAKE_UP:         return 1;
    case PC_CMD_PWR_RESET:       return 2;
    case PC_CMD_FORCE_POWER_OFF: return 3;
    default:                     return -1;   /* software functions */
    }
}

