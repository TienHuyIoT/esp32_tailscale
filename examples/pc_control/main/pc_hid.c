/**
 * @file pc_hid.c
 * @brief USB HID keyboard login (TinyUSB) over the ESP32-S3 native USB port.
 *
 * See pc_hid.h for the security contract. The password only ever lives in
 * s_buf (RAM), is zeroized after typing/cancel/error and is never logged.
 *
 * Hardware note: TinyUSB and the USB-Serial-JTAG controller share the same
 * USB pins (GPIO19/20). sdkconfig.defaults therefore disables the USJ
 * driver + its secondary console; the serial console, flashing and
 * debugging stay on UART0 (external USB-UART bridge - COM9).
 *
 * Typing uses US-layout HID keycodes: the characters that appear on the
 * target depend on the keyboard layout ACTIVE at that moment on Windows
 * (same as a physical keyboard).
 */

#include "pc_hid.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pc_hid";

void pc_hid_wipe(void *p, size_t n) {
    if (!p || !n) return;
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

#if CONFIG_PCCTL_ENABLE_HID

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#define HID_MAX_LEN  (CONFIG_PCCTL_HID_MAX_LEN)
#define ARM_DELAY_MS (CONFIG_PCCTL_HID_ARM_DELAY_MS)
#define KEY_DELAY_MS (CONFIG_PCCTL_HID_KEY_DELAY_MS)

/* ---------------- USB descriptors (keyboard only) ---------------- */

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

/* Standard boot-style keyboard report; no report IDs. */
static const uint8_t s_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};

static const char *s_string_desc[] = {
    (const char[]){0x09, 0x04}, /* en-US */
    "MicroLink",
    "PC Control (HID keyboard)",
    "pcctl-hid",
    "HID keyboard",
};

static const uint8_t s_config_desc[] = {
    /* config, interfaces, str idx, total len, attributes (bus powered), 100 mA */
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, 0x80, 100),
    /* interface, str idx, boot protocol, report desc len, EP in, size, interval */
    TUD_HID_DESCRIPTOR(0, 4, false, (uint8_t)sizeof(s_report_desc), 0x81, 16, 10),
};

/* ---------------- TinyUSB callbacks ---------------- */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    /* Keyboard LED state from the host - nothing to do. */
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}

/* ---------------- US-layout key map ---------------- */

typedef struct {
    uint8_t mod;    /* modifier byte, 0 or KEYBOARD_MODIFIER_LEFTSHIFT */
    uint8_t usage;  /* HID usage id */
} hid_key_t;

#define SHIFT KEYBOARD_MODIFIER_LEFTSHIFT

/* Printable ASCII (0x20..0x7E) to (modifier, HID usage). Reject the rest. */
static bool ascii_to_key(char c, hid_key_t *k) {
    if (c >= 'a' && c <= 'z') { k->mod = 0; k->usage = (uint8_t)(HID_KEY_A + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { k->mod = SHIFT; k->usage = (uint8_t)(HID_KEY_A + (c - 'A')); return true; }
    if (c >= '1' && c <= '9') { k->mod = 0; k->usage = (uint8_t)(HID_KEY_1 + (c - '1')); return true; }
    switch (c) {
    case ' ':  k->mod = 0;     k->usage = HID_KEY_SPACE; return true;
    case '0':  k->mod = 0;     k->usage = HID_KEY_0; return true;
    case '!':  k->mod = SHIFT; k->usage = HID_KEY_1; return true;
    case 0x22: k->mod = SHIFT; k->usage = HID_KEY_APOSTROPHE; return true; /* double quote */
    case '#':  k->mod = SHIFT; k->usage = HID_KEY_3; return true;
    case '$':  k->mod = SHIFT; k->usage = HID_KEY_4; return true;
    case '%':  k->mod = SHIFT; k->usage = HID_KEY_5; return true;
    case '&':  k->mod = SHIFT; k->usage = HID_KEY_7; return true;
    case 0x27: k->mod = 0;     k->usage = HID_KEY_APOSTROPHE; return true; /* apostrophe */
    case '(':  k->mod = SHIFT; k->usage = HID_KEY_9; return true;
    case ')':  k->mod = SHIFT; k->usage = HID_KEY_0; return true;
    case '*':  k->mod = SHIFT; k->usage = HID_KEY_8; return true;
    case '+':  k->mod = SHIFT; k->usage = HID_KEY_EQUAL; return true;
    case ',':  k->mod = 0;     k->usage = HID_KEY_COMMA; return true;
    case '-':  k->mod = 0;     k->usage = HID_KEY_MINUS; return true;
    case '.':  k->mod = 0;     k->usage = HID_KEY_PERIOD; return true;
    case '/':  k->mod = 0;     k->usage = HID_KEY_SLASH; return true;
    case ':':  k->mod = SHIFT; k->usage = HID_KEY_SEMICOLON; return true;
    case ';':  k->mod = 0;     k->usage = HID_KEY_SEMICOLON; return true;
    case '<':  k->mod = SHIFT; k->usage = HID_KEY_COMMA; return true;
    case '=':  k->mod = 0;     k->usage = HID_KEY_EQUAL; return true;
    case 0x3E: k->mod = SHIFT; k->usage = HID_KEY_PERIOD; return true; /* greater-than */
    case '?':  k->mod = SHIFT; k->usage = HID_KEY_SLASH; return true;
    case '@':  k->mod = SHIFT; k->usage = HID_KEY_2; return true;
    case '[':  k->mod = 0;     k->usage = HID_KEY_BRACKET_LEFT; return true;
    case 0x5C: k->mod = 0;     k->usage = HID_KEY_BACKSLASH; return true; /* backslash */
    case ']':  k->mod = 0;     k->usage = HID_KEY_BRACKET_RIGHT; return true;
    case '^':  k->mod = SHIFT; k->usage = HID_KEY_6; return true;
    case '_':  k->mod = SHIFT; k->usage = HID_KEY_MINUS; return true;
    case '`':  k->mod = 0;     k->usage = HID_KEY_GRAVE; return true;
    case '{':  k->mod = SHIFT; k->usage = HID_KEY_BRACKET_LEFT; return true;
    case '|':  k->mod = SHIFT; k->usage = HID_KEY_BACKSLASH; return true;
    case '}':  k->mod = SHIFT; k->usage = HID_KEY_BRACKET_RIGHT; return true;
    case '~':  k->mod = SHIFT; k->usage = HID_KEY_GRAVE; return true;
    default: return false;
    }
}
/* ---------------- job state ---------------- */

static char s_buf[HID_MAX_LEN + 1];
static volatile size_t s_len = 0;
static volatile bool s_enter = false;
static volatile bool s_cancel = false;
static volatile pc_hid_state_t s_state = PC_HID_IDLE;
static volatile int64_t s_state_us = 0;
static char s_last_err[48] = "";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task = NULL;

static void set_state(pc_hid_state_t st, const char *err) {
    portENTER_CRITICAL(&s_mux);
    s_state = st;
    s_state_us = esp_timer_get_time();
    snprintf(s_last_err, sizeof(s_last_err), "%s", err ? err : "");
    portEXIT_CRITICAL(&s_mux);
}

static void job_done_wipe(void) {
    pc_hid_wipe(s_buf, sizeof(s_buf));
    s_len = 0;
    s_enter = false;
    s_cancel = false;
}

static bool send_key(uint8_t mod, uint8_t usage) {
    uint8_t kc[6] = { usage, 0, 0, 0, 0, 0 };
    int waited_ms = 0;
    while (!tud_hid_ready() && waited_ms < 500) {
        vTaskDelay(pdMS_TO_TICKS(5));
        waited_ms += 5;
    }
    if (!tud_mounted()) return false;
    tud_hid_keyboard_report(0, mod, kc);
    vTaskDelay(pdMS_TO_TICKS(KEY_DELAY_MS));
    tud_hid_keyboard_report(0, 0, NULL);  /* release all keys */
    vTaskDelay(pdMS_TO_TICKS(KEY_DELAY_MS));
    return tud_mounted();  /* stop early when unplugged mid-sequence */
}

static void pc_hid_task(void *arg) {
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* ARM window: the job stays cancellable before the first keystroke. */
        if (ARM_DELAY_MS > 0) vTaskDelay(pdMS_TO_TICKS(ARM_DELAY_MS));
        if (s_cancel) {
            job_done_wipe();
            set_state(PC_HID_CANCELLED, NULL);
            continue;
        }
        if (!tud_mounted()) {
            job_done_wipe();
            set_state(PC_HID_ERROR, "usb not mounted");
            continue;
        }
        set_state(PC_HID_TYPING, NULL);
        bool ok = true;
        size_t n = s_len;
        for (size_t i = 0; i < n && ok; i++) {
            hid_key_t k;
            if (!ascii_to_key(s_buf[i], &k)) { ok = false; break; }
            ok = send_key(k.mod, k.usage);
        }
        if (ok && s_enter) ok = send_key(0, HID_KEY_ENTER);
        tud_hid_keyboard_report(0, 0, NULL);  /* final all-keys-release */
        job_done_wipe();
        if (ok) set_state(PC_HID_DONE, NULL);
        else set_state(PC_HID_ERROR, "usb disconnected mid-typing");
    }
}

esp_err_t pc_hid_start(void) {
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = NULL;
    cfg.descriptor.full_speed_config = s_config_desc;
    cfg.descriptor.string = s_string_desc;
    cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);
#if (TUD_OPT_HIGH_SPEED)
    cfg.descriptor.high_speed_config = s_config_desc;
#endif
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(pc_hid_task, "pc_hid", 3584, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "typing task creation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB HID keyboard ready on the native port - typing only on explicit request");
    return ESP_OK;
}

pc_hid_submit_result_t pc_hid_submit(const char *text, bool press_enter, size_t *bad_index) {
    if (bad_index) *bad_index = 0;
    if (!text) return PC_HID_SUBMIT_BAD_CHAR;
    pc_hid_state_t st = s_state;
    if (st == PC_HID_QUEUED || st == PC_HID_TYPING) return PC_HID_SUBMIT_BUSY;
    if (!tud_mounted()) return PC_HID_SUBMIT_NOT_MOUNTED;
    size_t n = strlen(text);
    if (n == 0) return PC_HID_SUBMIT_BAD_CHAR;
    if (n > HID_MAX_LEN) return PC_HID_SUBMIT_TOO_LONG;
    for (size_t i = 0; i < n; i++) {
        hid_key_t k;
        if (!ascii_to_key(text[i], &k)) {
            if (bad_index) *bad_index = i;
            return PC_HID_SUBMIT_BAD_CHAR;
        }
    }
    memcpy(s_buf, text, n);
    s_buf[n] = '\0';
    s_len = n;
    s_enter = press_enter;
    s_cancel = false;
    set_state(PC_HID_QUEUED, NULL);
    xTaskNotifyGive(s_task);
    return PC_HID_SUBMIT_OK;
}

esp_err_t pc_hid_cancel(void) {
    esp_err_t res = ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_mux);
    if (s_state == PC_HID_QUEUED) {
        s_cancel = true;
        res = ESP_OK;
    }
    portEXIT_CRITICAL(&s_mux);
    return res;
}

bool pc_hid_enabled(void) { return true; }
bool pc_hid_mounted(void) { return tud_mounted(); }
bool pc_hid_busy(void) {
    pc_hid_state_t st = s_state;
    return st == PC_HID_QUEUED || st == PC_HID_TYPING;
}
pc_hid_state_t pc_hid_get_state(void) { return s_state; }
const char *pc_hid_last_error(void) { return s_last_err; }
float pc_hid_state_age_s(void) {
    int64_t us = s_state_us;
    if (!us) return 0.f;
    return (float)((esp_timer_get_time() - us) / 1000000.0);
}
size_t pc_hid_max_len(void) { return HID_MAX_LEN; }

#else  /* !CONFIG_PCCTL_ENABLE_HID - feature compiled out */

esp_err_t pc_hid_start(void) {
    ESP_LOGI(TAG, "USB HID login disabled (CONFIG_PCCTL_ENABLE_HID=n)");
    return ESP_OK;
}
bool pc_hid_enabled(void) { return false; }
bool pc_hid_mounted(void) { return false; }
bool pc_hid_busy(void) { return false; }
pc_hid_state_t pc_hid_get_state(void) { return PC_HID_IDLE; }
const char *pc_hid_last_error(void) { return "hid_disabled"; }
float pc_hid_state_age_s(void) { return 0.f; }
size_t pc_hid_max_len(void) { return 0; }
pc_hid_submit_result_t pc_hid_submit(const char *text, bool press_enter, size_t *bad_index) {
    (void)text; (void)press_enter;
    if (bad_index) *bad_index = 0;
    return PC_HID_SUBMIT_DISABLED;
}
esp_err_t pc_hid_cancel(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif /* CONFIG_PCCTL_ENABLE_HID */

const char *pc_hid_state_str(pc_hid_state_t st) {
    switch (st) {
    case PC_HID_QUEUED:    return "queued";
    case PC_HID_TYPING:    return "typing";
    case PC_HID_DONE:      return "done";
    case PC_HID_CANCELLED: return "cancelled";
    case PC_HID_ERROR:     return "error";
    default:               return "idle";
    }
}
