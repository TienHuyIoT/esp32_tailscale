/**
 * @file ml_prov.h
 * @brief MicroLink SoftAP setup mode (provisioning fallback)
 *
 * Brought up by the application when no known WiFi network is reachable.
 * Creates a setup AP (default 192.168.4.1) serving the config web UI plus a
 * captive-portal DNS responder, so WiFi credentials can be changed from a
 * phone/laptop without reflashing. See examples/basic_connect for the
 * reference integration and the CONFIG_ML_PROV_* Kconfig options.
 *
 * Gated by CONFIG_ML_PROV_ENABLE_SOFTAP in Kconfig (depends on
 * ML_ENABLE_CONFIG_HTTPD, since the setup AP serves that web UI).
 */

#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional overrides for ml_prov_start(); NULL = use Kconfig values. */
typedef struct {
    const char *ap_ssid_prefix;  /* NULL = CONFIG_ML_PROV_AP_SSID_PREFIX */
    const char *ap_password;     /* NULL = CONFIG_ML_PROV_AP_PASSWORD; "" = open */
    uint8_t     channel;         /* 0 = CONFIG_ML_PROV_AP_CHANNEL */
} ml_prov_config_t;

#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP

/**
 * @brief Start setup mode: setup AP + config web UI + captive-portal DNS.
 *
 * The application must have initialized WiFi (esp_wifi_init + at least one
 * call path to esp_wifi_start) before calling. While active:
 *  - the config web UI answers on http://<ap-ip>/ (no VPN needed)
 *  - captive-portal probes are redirected to it (the DNS responder answers
 *    every A query with the AP IP)
 *
 * Exit either by the user saving credentials + restarting the device, or by
 * calling ml_prov_stop() after WiFi reconnects (see examples/basic_connect
 * for the soft-recovery loop).
 *
 * @param cfg  Optional overrides, NULL = Kconfig defaults
 * @return ESP_OK on success, error if WiFi is not initialized or HTTP
 *         server / AP bring-up fails
 */
esp_err_t ml_prov_start(const ml_prov_config_t *cfg);

/**
 * @brief Stop setup mode: DNS responder, web UI, AP; back to STA-only WiFi.
 *
 * MUST be called before microlink_start() when leaving setup mode — both
 * own port 80. Safe to call when not active.
 */
void ml_prov_stop(void);

/** @brief True between ml_prov_start() and ml_prov_stop(). */
bool ml_prov_is_active(void);

/** @brief Setup AP SSID (e.g. "MicroLink-Setup-a1b2c3"), or NULL if inactive. */
const char *ml_prov_get_ap_ssid(void);

#else /* !CONFIG_ML_PROV_ENABLE_SOFTAP */

/* Stubs when setup mode is disabled (mirrors ml_config_httpd.h stubs) */
static inline esp_err_t ml_prov_start(const ml_prov_config_t *cfg) { (void)cfg; return ESP_ERR_NOT_SUPPORTED; }
static inline void ml_prov_stop(void) {}
static inline bool ml_prov_is_active(void) { return false; }
static inline const char *ml_prov_get_ap_ssid(void) { return NULL; }

#endif /* CONFIG_ML_PROV_ENABLE_SOFTAP */

#ifdef __cplusplus
}
#endif
