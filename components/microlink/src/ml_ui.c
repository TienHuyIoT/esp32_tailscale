/**
 * @file ml_ui.c
 * @brief Shared ISUI web theme - cached static assets (/ml/static/) and the
 *        page-serving helper (see ml_ui.h).
 *
 * Moved out of examples/pc_control/main/pc_api.c so that the component's own
 * config page ("/") and the pc_control pages share one asset set, one cache
 * policy and one URL space. The theme CSS/JS themselves live in
 * ml_ui_theme.h and are only compiled into this translation unit.
 */

#include "sdkconfig.h"

#ifdef CONFIG_ML_ENABLE_CONFIG_HTTPD

#include "ml_ui.h"
#include "ml_ui_theme.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <string.h>

/* Build the shared-asset URL version from the app description (firmware
 * version + build date/time): every flash automatically busts the browser
 * cache of the /ml/static/ assets without manual bookkeeping. */
void ml_ui_asset_version(char *out, size_t out_len) {
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(out, out_len, "%s-%s-%s", app->version, app->date, app->time);
    for (char *p = out; *p; p++) {
        if (*p == ' ') *p = '-';
    }
}

/* Serve one embedded UI page: doctype + title + the shared theme CSS and the
 * helper scripts as separate long-lived cacheable assets (under /ml/static/,
 * versioned URL) + the page's own markup and script. Switching pages then
 * re-downloads only this (small, uncached) HTML - the shared ~65 KB of
 * CSS/JS comes from the browser cache and is never re-parsed from network. */
esp_err_t ml_ui_serve_page(httpd_req_t *req, const char *title,
                           const char *markup, const char *script) {
    char ver[48];
    ml_ui_asset_version(ver, sizeof(ver));
    char css_tag[128];
    snprintf(css_tag, sizeof(css_tag),
             "<link rel=\"stylesheet\" href=\"/ml/static/theme.css?v=%s\"></head>", ver);
    char js_tag1[128];
    snprintf(js_tag1, sizeof(js_tag1),
             "<script src=\"/ml/static/ui.js?v=%s\"></script>", ver);
    char js_tag2[128];
    snprintf(js_tag2, sizeof(js_tag2),
             "<script src=\"/ml/static/space.js?v=%s\"></script>", ver);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"dark\">");
    httpd_resp_sendstr_chunk(req, title);
    httpd_resp_sendstr_chunk(req, css_tag);
    httpd_resp_sendstr_chunk(req, markup);
    httpd_resp_sendstr_chunk(req, js_tag1);
    httpd_resp_sendstr_chunk(req, js_tag2);
    httpd_resp_sendstr_chunk(req, "<script>");
    httpd_resp_sendstr_chunk(req, script);
    httpd_resp_sendstr_chunk(req, "</script></body></html>");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/* GET /ml/static/ - theme.css / ui.js / space.js, served from the shared
 * theme header with a long-lived immutable cache. */
esp_err_t ml_ui_static_handler(httpd_req_t *req) {
    const char *prefix = "/ml/static/";
    size_t n = strlen(prefix);
    const char *rest = (strncmp(req->uri, prefix, n) == 0) ? req->uri + n : NULL;
    const char *body = NULL;
    const char *type = NULL;
    if (rest) {
        /* req->uri includes the query string (?v=... cache buster): compare
         * only the part before '?' / '#'. */
        size_t len = strcspn(rest, "?#");
        if (len == 9 && strncmp(rest, "theme.css", len) == 0) {
            body = ML_UI_CSS; type = "text/css";
        } else if (len == 5 && strncmp(rest, "ui.js", len) == 0) {
            body = ML_UI_JS; type = "application/javascript";
        } else if (len == 8 && strncmp(rest, "space.js", len) == 0) {
            body = ML_UI_SPACE_JS; type = "application/javascript";
        }
    }
    if (!body) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req, "{\"error\":\"not_found\"}");
    }
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    return httpd_resp_sendstr(req, body);
}

#endif /* CONFIG_ML_ENABLE_CONFIG_HTTPD */
