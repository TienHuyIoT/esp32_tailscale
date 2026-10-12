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
#define NVS_KEY_CONN  "conn"
#define NVS_KEY_HW    "hw"
#define CONN_VERSION  1
#define HW_VERSION    1

typedef struct __attribute__((packed)) {
    uint8_t  version;
    char     host[PC_CFG_HOST_MAX];
    uint16_t port;
    uint32_t timeout_ms;
    char     token[PC_CFG_TOKEN_MAX];
} pc_conn_blob_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint32_t funcs_mask;
    int8_t   gpio[PC_CFG_GPIO_SLOTS];
    uint16_t short_press_ms;
    uint16_t long_press_ms;
} pc_hw_blob_t;

/* Verified usable output pins for THIS device (see docs/PC_CONTROLLER.md
 * "GPIO policy"): ESP32-S3 rev v0.2, 8 MB octal PSRAM, 16 MB flash,
 * console on UART0 (external USB-UART bridge - COM9).
 * Excluded: 0/3/45/46 strapping, 19/20 USB-JTAG, 26-32 SPI flash,
 * 33-37 octal PSRAM, 38-42 JTAG, 43/44 UART0 console.
 * Adjust in code for a different board. */
static const int s_allowed_gpios[] = {
    1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 47, 48
};

static pc_conn_blob_t s_conn;
static pc_hw_blob_t s_hw;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static uint32_t default_funcs_mask(void) {
    /* initial states per the project plan: software functions enabled,
     * hardware functions disabled until enabled + GPIO-mapped by the user */
    return (1u << PC_CMD_SHUTDOWN) | (1u << PC_CMD_RESTART);
}

static void load_defaults(void) {
    memset(&s_conn, 0, sizeof(s_conn));
    memset(&s_hw, 0, sizeof(s_hw));
    s_conn.version = CONN_VERSION;
    snprintf(s_conn.host, sizeof(s_conn.host), "%s", CONFIG_PCCTL_WIN_HOST);
    s_conn.port = CONFIG_PCCTL_WIN_PORT;
    s_conn.timeout_ms = CONFIG_PCCTL_HTTP_TIMEOUT_MS;
    snprintf(s_conn.token, sizeof(s_conn.token), "%s", CONFIG_PCCTL_WIN_AGENT_TOKEN);
    s_hw.version = HW_VERSION;
    s_hw.funcs_mask = default_funcs_mask();
    for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) s_hw.gpio[i] = -1;
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

    pc_conn_blob_t conn;
    size_t len = sizeof(conn);
    if (nvs_get_blob(s_nvs, NVS_KEY_CONN, &conn, &len) == ESP_OK &&
        len == sizeof(conn) && conn.version == CONN_VERSION &&
        conn.host[0] != '\0' && conn.port != 0) {
        s_conn = conn;
        ESP_LOGI(TAG, "connection config from NVS: host=%s port=%u",
                 s_conn.host, (unsigned)s_conn.port);
    } else {
        ESP_LOGI(TAG, "no valid NVS connection config - Kconfig defaults apply");
    }

    pc_hw_blob_t hw;
    len = sizeof(hw);
    if (nvs_get_blob(s_nvs, NVS_KEY_HW, &hw, &len) == ESP_OK &&
        len == sizeof(hw) && hw.version == HW_VERSION &&
        hw.short_press_ms >= PC_CFG_SHORT_PRESS_MIN &&
        hw.long_press_ms >= PC_CFG_LONG_PRESS_MIN) {
        s_hw = hw;
        ESP_LOGI(TAG, "hardware config from NVS: funcs=0x%02lx gpio=[%d,%d,%d,%d] short=%u long=%u",
                 (unsigned long)s_hw.funcs_mask, s_hw.gpio[0], s_hw.gpio[1],
                 s_hw.gpio[2], s_hw.gpio[3],
                 (unsigned)s_hw.short_press_ms, (unsigned)s_hw.long_press_ms);
    } else {
        ESP_LOGI(TAG, "no valid NVS hardware config - plan defaults apply");
    }
    ESP_LOGI(TAG, "effective config: host=%s port=%u token=%s",
             s_conn.host, (unsigned)s_conn.port,
             s_conn.token[0] ? "set" : "(not set)");
}

void pc_config_get(pc_config_snapshot_t *out) {
    if (!out) return;
    lock();
    memset(out, 0, sizeof(*out));
    snprintf(out->host, sizeof(out->host), "%s", s_conn.host);
    out->port = s_conn.port;
    out->timeout_ms = s_conn.timeout_ms;
    out->token_set = (s_conn.token[0] != '\0');
    out->funcs_mask = s_hw.funcs_mask;
    for (int i = 0; i < PC_CFG_GPIO_SLOTS; i++) out->gpio[i] = s_hw.gpio[i];
    out->short_press_ms = s_hw.short_press_ms;
    out->long_press_ms = s_hw.long_press_ms;
    unlock();
}

bool pc_config_token(char *out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    lock();
    bool set = (s_conn.token[0] != '\0');
    if (set) snprintf(out, out_len, "%s", s_conn.token);
    unlock();
    return set;
}

static bool host_valid(const char *h) {
    size_t n = strlen(h);
    if (n == 0 || n >= PC_CFG_HOST_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}

static bool token_valid(const char *t) {
    size_t n = strlen(t);
    if (n == 0) return true;                        /* empty = clear */
    if (n < 4 || n >= PC_CFG_TOKEN_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (c <= ' ' || c > 0x7e) return false;     /* no spaces/control/UTF-8 */
    }
    return true;
}

static bool gpio_conflicts(const int8_t pins[PC_CFG_GPIO_SLOTS]) {
    /* RESET_SW must be a different output than any PWR_SW-family pin
     * (POWER_ON / WAKE_UP / FORCE_POWER_OFF may share one PWR_SW line). */
    if (pins[2] >= 0) {
        for (int s = 0; s < PC_CFG_GPIO_SLOTS; s++) {
            if (s != 2 && pins[s] == pins[2]) return true;
        }
    }
    return false;
}

esp_err_t pc_config_apply(const pc_config_patch_t *patch) {
    if (!patch) return ESP_ERR_INVALID_ARG;

    pc_conn_blob_t conn;
    pc_hw_blob_t hw;
    lock();
    conn = s_conn;
    hw = s_hw;
    unlock();

    if (patch->set_host) {
        if (!host_valid(patch->host)) return ESP_ERR_INVALID_ARG;
        snprintf(conn.host, sizeof(conn.host), "%s", patch->host);
    }
    if (patch->set_port) {
        if (patch->port < 1 || patch->port > 65535) return ESP_ERR_INVALID_ARG;
        conn.port = (uint16_t)patch->port;
    }
    if (patch->set_timeout) {
        if (patch->timeout_ms < PC_CFG_TIMEOUT_MIN_MS ||
            patch->timeout_ms > PC_CFG_TIMEOUT_MAX_MS) return ESP_ERR_INVALID_ARG;
        conn.timeout_ms = (uint32_t)patch->timeout_ms;
    }
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
    if (gpio_conflicts(hw.gpio)) return ESP_ERR_INVALID_STATE;
    if (patch->set_timings) {
        if (patch->short_ms < PC_CFG_SHORT_PRESS_MIN ||
            patch->short_ms > PC_CFG_SHORT_PRESS_MAX ||
            patch->long_ms < PC_CFG_LONG_PRESS_MIN ||
            patch->long_ms > PC_CFG_LONG_PRESS_MAX) return ESP_ERR_INVALID_ARG;
        hw.short_press_ms = (uint16_t)patch->short_ms;
        hw.long_press_ms = (uint16_t)patch->long_ms;
    }

    /* Persist first; apply to RAM only after both blobs are stored, so a
     * failed write never leaves a partially-applied configuration. */
    esp_err_t err = persist_blob(NVS_KEY_CONN, &conn, sizeof(conn));
    if (err != ESP_OK) return err;
    err = persist_blob(NVS_KEY_HW, &hw, sizeof(hw));
    if (err != ESP_OK) return err;

    lock();
    s_conn = conn;
    s_hw = hw;
    unlock();
    ESP_LOGI(TAG, "config applied: host=%s port=%u funcs=0x%02lx gpio=[%d,%d,%d,%d]",
             conn.host, (unsigned)conn.port, (unsigned long)hw.funcs_mask,
             hw.gpio[0], hw.gpio[1], hw.gpio[2], hw.gpio[3]);
    return ESP_OK;
}

esp_err_t pc_config_set_token(const char *token) {
    if (!token) token = "";
    if (!token_valid(token)) return ESP_ERR_INVALID_ARG;

    pc_conn_blob_t conn;
    lock();
    conn = s_conn;
    unlock();
    snprintf(conn.token, sizeof(conn.token), "%s", token);

    esp_err_t err = persist_blob(NVS_KEY_CONN, &conn, sizeof(conn));
    if (err != ESP_OK) return err;

    lock();
    snprintf(s_conn.token, sizeof(s_conn.token), "%s", token);
    unlock();
    ESP_LOGI(TAG, "agent token %s", token[0] ? "updated" : "cleared");
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

