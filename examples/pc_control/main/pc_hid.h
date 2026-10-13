/**
 * @file pc_hid.h
 * @brief USB HID keyboard login over the ESP32-S3 native USB port.
 *
 * Security contract (see docs/PC_CONTROLLER.md "USB HID login"):
 *  - the submitted password lives ONLY in a RAM buffer owned by this
 *    module; it is zeroized after typing, cancellation or error;
 *  - it is never written to NVS/flash, never logged and never echoed in
 *    any HTTP response or status payload;
 *  - typing starts only after an explicit, token-gated API request, and
 *    only after a short ARM window during which the job can be cancelled.
 *
 * Build gate: CONFIG_PCCTL_ENABLE_HID. When disabled, no TinyUSB driver
 * is installed and every entry point reports "disabled".
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_HID_IDLE = 0,
    PC_HID_QUEUED,      /* accepted, inside the ARM window - cancellable */
    PC_HID_TYPING,      /* keystrokes are being sent - not cancellable */
    PC_HID_DONE,        /* all keystrokes sent (Windows outcome unknown) */
    PC_HID_CANCELLED,   /* cancelled during the ARM window */
    PC_HID_ERROR,       /* aborted (USB unplugged, driver error, ...) */
} pc_hid_state_t;

typedef enum {
    PC_HID_SUBMIT_OK = 0,
    PC_HID_SUBMIT_BUSY,         /* another job is queued or typing */
    PC_HID_SUBMIT_NOT_MOUNTED,  /* USB host has not enumerated us */
    PC_HID_SUBMIT_DISABLED,     /* CONFIG_PCCTL_ENABLE_HID is off */
    PC_HID_SUBMIT_TOO_LONG,     /* longer than CONFIG_PCCTL_HID_MAX_LEN */
    PC_HID_SUBMIT_BAD_CHAR,     /* non-printable-ASCII, see *bad_index */
} pc_hid_submit_result_t;

/** Install the TinyUSB HID keyboard + typing task. Call once at boot.
 *  No-op (ESP_OK) when the feature is compiled out. */
esp_err_t pc_hid_start(void);

bool pc_hid_enabled(void);  /* compiled in */
bool pc_hid_mounted(void);  /* enumerated by a USB host (PC) */
bool pc_hid_busy(void);     /* QUEUED or TYPING */

pc_hid_state_t pc_hid_get_state(void);
const char *pc_hid_state_str(pc_hid_state_t st);
const char *pc_hid_last_error(void);   /* static string, never secret */
float pc_hid_state_age_s(void);        /* seconds since the last change */
size_t pc_hid_max_len(void);           /* CONFIG_PCCTL_HID_MAX_LEN */

/** Validate + copy `text` into the module's RAM buffer and queue it.
 *  `press_enter` appends an ENTER keystroke after the text.
 *  On PC_HID_SUBMIT_BAD_CHAR, *bad_index (0-based) is set - the
 *  character itself is never reported back. */
pc_hid_submit_result_t pc_hid_submit(const char *text, bool press_enter,
                                     size_t *bad_index);

/** Cancel a job inside the ARM window (state QUEUED).
 *  ESP_ERR_INVALID_STATE when nothing cancellable is pending. */
esp_err_t pc_hid_cancel(void);

/** Best-effort wipe for secret buffers (volatile store loop).
 *  Also used by pc_api.c for request-body/JSON buffers. */
void pc_hid_wipe(void *p, size_t n);

#ifdef __cplusplus
}
#endif