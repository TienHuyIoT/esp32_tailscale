/**
 * @file pc_auth.h
 * @brief Device web login (admin account) + runtime API token.
 *
 * The /pc/security page is protected by a username/password login (default
 * admin/admin, changeable from the page). The API token used by the control
 * and settings pages (X-PC-Token) is stored at runtime in NVS so it can be
 * generated/rotated without reflashing; when no runtime token is set, the
 * compile-time CONFIG_PCCTL_API_TOKEN is used as the default.
 *
 * Storage: NVS namespace "pc_ctl", keys "auth" (versioned blob) and
 * "api_token" (string). Following the project rule, nothing is written to
 * NVS until the user performs an explicit action (a credential change or an
 * API-token generate/set/clear).
 *
 * Passwords are stored salted (16-byte random salt) and hashed with SHA-256;
 * the plaintext is never stored or logged. Sessions live in RAM only and
 * vanish on reboot.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_AUTH_USER_MAX      32    /* username chars (buffer = +1) */
#define PC_AUTH_PASS_MIN      1
#define PC_AUTH_PASS_MAX      64
#define PC_AUTH_TOKEN_MIN     4
#define PC_AUTH_TOKEN_MAX     96    /* API token chars (buffer = +1) */
#define PC_AUTH_SESSION_TTL_S 1800  /* 30 min sliding window */

/** One-time init after nvs_flash_init(). Activates the admin/admin default
 *  (RAM only) and loads any stored runtime API token. */
void pc_auth_init(void);

/* --- admin login (the /pc/security page) --- */
/** Verify username + password. */
bool pc_auth_verify(const char *username, const char *password);

/** Change the login credentials. `current_password` must match the active
 *  password. `new_username` / `new_password` may be NULL or empty to keep the
 *  current value. Returns ESP_ERR_INVALID_ARG on validation failure,
 *  ESP_ERR_INVALID_STATE when the current password is wrong, ESP_OK on
 *  success (persisted). */
esp_err_t pc_auth_change_credentials(const char *current_password,
                                     const char *new_username,
                                     const char *new_password);

/** Copy the current admin username (never the password). */
void pc_auth_username(char *out, size_t out_len);

/* --- sessions (RAM only; cookie "pcctl_sess") --- */
bool pc_auth_session_create(char *out, size_t out_len);
bool pc_auth_session_valid(const char *id);
void pc_auth_session_destroy(const char *id);

/* --- runtime API token (browser -> ESP32 mutations) --- */
/** Effective token: the NVS runtime token, else CONFIG_PCCTL_API_TOKEN.
 *  Returns false when none is configured (out[0] = '\0'). */
bool pc_auth_api_token(char *out, size_t out_len);

/** True when a runtime token is stored in NVS (overrides the Kconfig one). */
bool pc_auth_api_token_overridden(void);

/** Store a runtime API token (persisted). Empty string clears the override. */
esp_err_t pc_auth_set_api_token(const char *token);

/** Generate a fresh random token, persist it and return it once. `out` must
 *  hold at least PC_AUTH_TOKEN_MAX + 1 bytes. */
esp_err_t pc_auth_generate_api_token(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
