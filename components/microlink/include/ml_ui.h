/**
 * @file ml_ui.h
 * @brief Shared ISUI web theme - cached static assets (/ml/static/) and the
 *        page-serving helper for every MicroLink web page.
 *
 * The theme CSS and helper scripts (ml_ui_theme.h) are served as versioned,
 * long-lived cacheable assets so that the component config page ("/") and
 * the pc_control pages (/pc, /pc/settings, /pc/security) share one asset
 * set: switching pages re-downloads only the small uncached HTML, while the
 * shared ~65 KB of CSS/JS stays in the browser cache until the next flash
 * (the version is derived from the app description).
 *
 * Gated by CONFIG_ML_ENABLE_CONFIG_HTTPD in Kconfig; safe stubs are provided
 * when the config HTTP server is disabled.
 */

#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include <stddef.h>

#ifdef CONFIG_ML_ENABLE_CONFIG_HTTPD

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill `out` with the shared-asset URL version
 *
 * Firmware version + build date/time with spaces turned into dashes, so
 * every flash automatically busts the browser cache of /ml/static/ assets.
 */
void ml_ui_asset_version(char *out, size_t out_len);

/**
 * @brief Serve one embedded UI page over the existing HTTP server
 *
 * Emits doctype + head (charset, viewport, dark color-scheme, title), the
 * shared theme <link> and helper <script> tags (versioned /ml/static/ URLs)
 * plus the page's own markup and script. Only the HTML is uncached - the
 * shared CSS/JS come from the browser cache.
 */
esp_err_t ml_ui_serve_page(httpd_req_t *req, const char *title,
                           const char *markup, const char *script);

/**
 * @brief GET /ml/static/ - serve theme.css / ui.js / space.js
 *
 * Long-lived immutable cache (public, max-age=31536000). The ?v= cache
 * buster query string is ignored; unknown names return 404 JSON.
 */
esp_err_t ml_ui_static_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

#else /* !CONFIG_ML_ENABLE_CONFIG_HTTPD */

/* Stubs when the config HTTP server is disabled (never reached in practice:
 * without the server there is nothing to serve pages on). */
static inline void ml_ui_asset_version(char *out, size_t out_len) {
    (void)out; (void)out_len;
}
static inline esp_err_t ml_ui_serve_page(httpd_req_t *req, const char *title,
                                         const char *markup, const char *script) {
    (void)req; (void)title; (void)markup; (void)script;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t ml_ui_static_handler(httpd_req_t *req) {
    (void)req;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_ML_ENABLE_CONFIG_HTTPD */
