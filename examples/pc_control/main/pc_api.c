/**
 * @file pc_api.c
 * @brief PC-control HTTP API + embedded UI, registered on the existing
 *        port-80 server (see ml_config_httpd_get_handle()).
 *
 * Routes:
 *   GET  /pc                          embedded control UI
 *   GET  /pc/settings                 settings UI
 *   GET  /pc/security                 login + API-token management UI
 *   GET  /api/v1/status               firmware/PC state
 *   GET  /api/v1/commands             command list + availability
 *   POST /api/v1/commands             submit command (token required)
 *   GET  /api/v1/commands/{id}        command state
 *   POST /api/v1/hid/login            type the password over USB HID
 *                                     (token required; RAM-only)
 *   POST /api/v1/hid/cancel           cancel the pre-typing ARM window
 *   POST /api/v1/auth/login           admin login (sets a session cookie)
 *   POST /api/v1/auth/logout          drop the session
 *   GET  /api/v1/auth/status          login + API-token state
 *   POST /api/v1/auth/credentials     change login (session required)
 *   GET  /api/v1/auth/api-token       API-token state (session required)
 *   POST /api/v1/auth/api-token       generate/set/clear the API token (session)
 *   POST /api/v1/device/reboot        restart the ESP32 (token required; all
 *                                     configuration is kept)
 *   GET  /api/v1/relays               relay channels: config + commanded state
 *   POST /api/v1/relays               explicit ON/OFF for one relay channel
 *   POST /api/v1/relays/config        relay channel configuration (Settings)

 *
 * Mutations require X-PC-Token (or Authorization: Bearer) to match the runtime
 * API token stored in NVS, falling back to CONFIG_PCCTL_API_TOKEN. Plain HTTP
 * is acceptable only because traffic stays inside the WireGuard tunnel; the
 * token still gates every mutation (tailnet membership alone is not treated as
 * authorization).
 */

#include "pc_api.h"
#include "pc_web.h"
#include "pc_security_web.h"
#include "pc_controller.h"
#include "pc_config.h"
#include "pc_auth.h"
#include "pc_state.h"
#include "pc_cmd.h"
#include "pc_hid.h"
#include "pc_relay.h"
#include "ml_config_httpd.h"
#include "ml_ui.h"
#include "microlink_internal.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "pc_api";

#define MAX_BODY_LEN 4096
#define MAX_ID_LEN   32

/* ---------- response helpers ---------- */

static esp_err_t send_json_status(httpd_req_t *req, const char *status, cJSON *json) {
    if (status) httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON error");
        return ESP_FAIL;
    }
    esp_err_t err = httpd_resp_sendstr(req, str);
    free(str);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status,
                            const char *error, const char *detail) {
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(json, "ok", false);
    cJSON_AddStringToObject(json, "error", error);
    if (detail) cJSON_AddStringToObject(json, "detail", detail);
    return send_json_status(req, status, json);
}

/* ---------- embedded ISUI pages ---------- */
/* Page serving + the shared theme assets (/ml/static/theme.css, /ml/static/
 * ui.js, /ml/static/space.js) live in the microlink component now
 * (ml_ui.c / ml_ui.h): the component's own config page and these /pc pages
 * share one theme, one asset cache and one URL space. */

/* ---------- request helpers ---------- */

static char *read_body(httpd_req_t *req) {
    int len = req->content_len;
    if (len <= 0 || len > MAX_BODY_LEN) return NULL;
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    int received = 0;
    while (received < len) {
        int r = httpd_req_recv(req, buf + received, len - received);
        if (r <= 0) {
            free(buf);
            return NULL;
        }
        received += r;
    }
    buf[len] = '\0';
    return buf;
}

static bool content_type_is_json(httpd_req_t *req) {
    char ct[64];
    if (httpd_req_get_hdr_value_len(req, "Content-Type") == 0) return false;
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, sizeof(ct)) != ESP_OK) return false;
    return strncmp(ct, "application/json", 16) == 0;
}

static bool id_is_valid(const char *id) {
    size_t n = strlen(id);
    if (n == 0 || n > MAX_ID_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

/* ---------- auth ---------- */

static bool ct_equal(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la != lb) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < la; i++) diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return diff == 0;
}

static bool get_presented_token(httpd_req_t *req, char *buf, size_t len) {
    if (httpd_req_get_hdr_value_len(req, "X-PC-Token") > 0 &&
        httpd_req_get_hdr_value_str(req, "X-PC-Token", buf, len) == ESP_OK) {
        return true;
    }
    char auth[128];
    if (httpd_req_get_hdr_value_len(req, "Authorization") > 0 &&
        httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) == ESP_OK &&
        strncmp(auth, "Bearer ", 7) == 0) {
        snprintf(buf, len, "%s", auth + 7);
        return true;
    }
    return false;
}

/* Gate every mutation. Fills *status and *error on rejection. */
static bool authorize_mutation(httpd_req_t *req, const char **status, const char **error) {
    char configured[PC_AUTH_TOKEN_MAX + 1];
    if (!pc_auth_api_token(configured, sizeof(configured))) {
#ifdef CONFIG_PCCTL_ALLOW_NO_TOKEN
        return true;
#else
        *status = "403 Forbidden";
        *error = "api_token_not_configured";
        return false;
#endif
    }
    char presented[128];
    if (!get_presented_token(req, presented, sizeof(presented))) {
        *status = "401 Unauthorized";
        *error = "missing_token";
        return false;
    }
    if (!ct_equal(presented, configured)) {
        *status = "401 Unauthorized";
        *error = "invalid_token";
        return false;
    }
    return true;
}

/* ---------- JSON builders ---------- */

static cJSON *slot_to_json(const pc_cmd_slot_t *s) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    const pc_cmd_def_t *def = pc_cmd_def(s->id);
    int64_t now = esp_timer_get_time();
    cJSON_AddStringToObject(o, "request_id", s->request_id);
    cJSON_AddStringToObject(o, "command", def ? def->name : "UNKNOWN");
    cJSON_AddStringToObject(o, "state", pc_cmd_state_str(s->state));
    cJSON_AddNumberToObject(o, "age_s", (double)(now - s->created_us) / 1e6);
    cJSON_AddNumberToObject(o, "duration_ms",
                            (double)(s->updated_us - s->created_us) / 1000.0);
    if (s->detail[0]) cJSON_AddStringToObject(o, "detail", s->detail);
    if (s->error[0]) cJSON_AddStringToObject(o, "error", s->error);
    return o;
}

/* ---------- relay helpers ---------- */

static cJSON *relay_channel_to_json(const pc_relay_snapshot_t *snap, uint8_t id) {
    const pc_relay_channel_t *ch = &snap->channels[id];
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "id", id);
    cJSON_AddStringToObject(o, "name", ch->name);
    cJSON_AddBoolToObject(o, "enabled", ch->enabled);
    if (ch->gpio >= 0) cJSON_AddNumberToObject(o, "gpio", ch->gpio);
    else cJSON_AddNullToObject(o, "gpio");
    cJSON_AddStringToObject(o, "active_level",
                            ch->active_level == PC_CFG_LEVEL_LOW ? "low" : "high");
    cJSON_AddStringToObject(o, "startup_policy", "off");
    cJSON_AddStringToObject(o, "state", ch->state ? "on" : "off");
    const char *reason = NULL;
    bool available = pc_relay_channel_available(snap, id, &reason);
    cJSON_AddBoolToObject(o, "available", available);
    if (!available && reason) cJSON_AddStringToObject(o, "unavailable_reason", reason);
    return o;
}

/* Relay block for JSON responses. `full` includes every channel slot (the
 * Settings page / API listing); otherwise only the active channels (the
 * control page polls /api/v1/status). Static snapshot: handlers run on the
 * shared httpd task (same rationale as the other static snapshots). */
static cJSON *relays_to_json(bool full) {
    static pc_relay_snapshot_t snap;
    pc_relay_get(&snap);
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    cJSON_AddNumberToObject(json, "channel_count", snap.channel_count);
    cJSON_AddNumberToObject(json, "max_channels", snap.max_channels);
    cJSON *arr = cJSON_AddArrayToObject(json, "channels");
    int n = full ? snap.max_channels : snap.channel_count;
    for (int i = 0; i < n && arr; i++) {
        cJSON *c = relay_channel_to_json(&snap, (uint8_t)i);
        if (c) cJSON_AddItemToArray(arr, c);
    }
    return json;
}

/* ---------- GET /api/v1/status ---------- */

static esp_err_t handler_status(httpd_req_t *req) {
    microlink_t *ml = (microlink_t *)req->user_ctx;
    /* static: handlers run on the shared httpd task; keeps the struct off its 6 KB stack */
    static pc_state_snapshot_t snap;
    pc_state_get(&snap);

    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(json, "ok", true);

    cJSON *fw = cJSON_AddObjectToObject(json, "fw");
    cJSON_AddStringToObject(fw, "name", "microlink_pc_control");
    cJSON_AddStringToObject(fw, "version", "0.1.0");
    cJSON_AddNumberToObject(fw, "uptime_s", (double)(esp_timer_get_time() / 1000000));

    cJSON *esp = cJSON_AddObjectToObject(json, "esp32");
    if (ml) {
        const char *states[] = {"IDLE", "WIFI_WAIT", "CONNECTING", "REGISTERING",
                                "CONNECTED", "RECONNECTING", "ERROR", "AUTH_FAILED"};
        int s = microlink_get_state(ml);
        cJSON_AddStringToObject(esp, "state", (s >= 0 && s < 8) ? states[s] : "UNKNOWN");
        char vpn[16] = "0.0.0.0";
        uint32_t vpn_ip = microlink_get_vpn_ip(ml);
        if (vpn_ip) microlink_ip_to_str(vpn_ip, vpn);
        cJSON_AddStringToObject(esp, "vpn_ip", vpn);
        cJSON_AddStringToObject(esp, "hostname",
            (ml->config.device_name && ml->config.device_name[0])
                ? ml->config.device_name : microlink_default_device_name());
    }
    char lan[16] = "";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(lan, sizeof(lan), "%u.%u.%u.%u",
                 (unsigned)(ip_info.ip.addr & 0xFF),
                 (unsigned)((ip_info.ip.addr >> 8) & 0xFF),
                 (unsigned)((ip_info.ip.addr >> 16) & 0xFF),
                 (unsigned)((ip_info.ip.addr >> 24) & 0xFF));
    }
    cJSON_AddStringToObject(esp, "wifi_ip", lan);
    cJSON_AddNumberToObject(esp, "free_heap", esp_get_free_heap_size());

    cJSON *pc = cJSON_AddObjectToObject(json, "pc");
    cJSON_AddStringToObject(pc, "state", pc_power_state_str(snap.power));
    cJSON_AddStringToObject(pc, "source", snap.power_source);
    cJSON_AddStringToObject(pc, "note", snap.power_note);

    if (snap.active.state != PC_CMD_STATE_NONE) {
        cJSON_AddItemToObject(json, "active_command", slot_to_json(&snap.active));
    } else {
        cJSON_AddNullToObject(json, "active_command");
    }
    if (snap.history_count > 0) {
        cJSON_AddItemToObject(json, "last_command", slot_to_json(&snap.history[0]));
    } else {
        cJSON_AddNullToObject(json, "last_command");
    }

    cJSON_AddNumberToObject(json, "hw_cooldown_ms", pc_controller_hw_cooldown_remaining_ms());

    cJSON *auth = cJSON_AddObjectToObject(json, "auth");
    char token[PC_AUTH_TOKEN_MAX + 1];
    bool configured = pc_auth_api_token(token, sizeof(token));
    cJSON_AddBoolToObject(auth, "token_configured", configured);
    cJSON_AddBoolToObject(auth, "token_custom", pc_auth_api_token_overridden());
#ifdef CONFIG_PCCTL_ALLOW_NO_TOKEN
    cJSON_AddBoolToObject(auth, "mutations_allowed", true);
#else
    cJSON_AddBoolToObject(auth, "mutations_allowed", configured);
#endif

    /* USB HID login readiness for the /pc HID panel. No secrets here. */
    cJSON *hid = cJSON_AddObjectToObject(json, "hid");
    cJSON_AddBoolToObject(hid, "enabled", pc_hid_enabled());
    cJSON_AddBoolToObject(hid, "mounted", pc_hid_mounted());
    cJSON_AddStringToObject(hid, "state", pc_hid_state_str(pc_hid_get_state()));
    cJSON_AddBoolToObject(hid, "busy", pc_hid_busy());
    cJSON_AddNumberToObject(hid, "max_len", (double)pc_hid_max_len());
    cJSON_AddNumberToObject(hid, "state_age_s", pc_hid_state_age_s());
    const char *hid_err = pc_hid_last_error();
    if (hid_err && hid_err[0]) cJSON_AddStringToObject(hid, "last_error", hid_err);

    /* Relay channels: configuration + COMMANDED states for the /pc page.
     * There is no contact feedback - "state" is the commanded logic level. */
    cJSON *relays = relays_to_json(false);
    if (relays) cJSON_AddItemToObject(json, "relays", relays);

    return send_json_status(req, NULL, json);
}

/* ---------- GET /api/v1/commands ---------- */

static esp_err_t handler_get_commands(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(json, "ok", true);

    cJSON *arr = cJSON_AddArrayToObject(json, "commands");
    for (int i = 0; i < PC_CMD_COUNT && arr; i++) {
        const pc_cmd_def_t *def = pc_cmd_def(i);
        bool available;
        const char *reason;
        pc_controller_command_availability(def, &available, &reason);

        cJSON *c = cJSON_CreateObject();
        if (!c) continue;
        cJSON_AddStringToObject(c, "id", def->name);
        cJSON_AddBoolToObject(c, "available", available);
        if (!available && reason) cJSON_AddStringToObject(c, "unavailable_reason", reason);
        cJSON_AddBoolToObject(c, "requires_confirm", def->requires_confirm);
        cJSON_AddStringToObject(c, "description", def->description);
        cJSON_AddItemToArray(arr, c);
    }

    /* static: same rationale as in handler_status */
    static pc_state_snapshot_t snap;
    pc_state_get(&snap);
    if (snap.active.state != PC_CMD_STATE_NONE) {
        cJSON_AddItemToObject(json, "active_command", slot_to_json(&snap.active));
    } else {
        cJSON_AddNullToObject(json, "active_command");
    }
    return send_json_status(req, NULL, json);
}

/* ---------- POST /api/v1/commands ---------- */

static esp_err_t handler_post_commands(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        const char *detail =
            (error && strcmp(error, "api_token_not_configured") == 0)
                ? "set CONFIG_PCCTL_API_TOKEN (sdkconfig.credentials or menuconfig)"
                : "send X-PC-Token or Authorization: Bearer";
        return send_error(req, status, error, detail);
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required",
                          "use Content-Type: application/json");
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    cJSON *cmd_item = cJSON_GetObjectItem(json, "command");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "missing_command", NULL);
    }
    /* PING: auth probe used by the embedded UI to verify a freshly entered
     * token without executing anything. It walks the same auth gate above
     * and touches no command state. */
    if (strcmp(cmd_item->valuestring, "PING") == 0) {
        cJSON_Delete(json);
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddBoolToObject(resp, "pong", true);
        return send_json_status(req, NULL, resp);
    }

    const pc_cmd_def_t *def = pc_cmd_lookup(cmd_item->valuestring);
    if (!def) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "unknown_command", NULL);
    }

    char request_id[PC_ID_MAX] = {0};
    cJSON *id_item = cJSON_GetObjectItem(json, "request_id");
    if (id_item) {
        if (!cJSON_IsString(id_item) || !id_is_valid(id_item->valuestring)) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_request_id",
                              "1-32 chars of [A-Za-z0-9_-]");
        }
        snprintf(request_id, sizeof(request_id), "%s", id_item->valuestring);
    }

    if (def->requires_confirm) {
        cJSON *conf = cJSON_GetObjectItem(json, "confirm");
        if (!conf || !cJSON_IsString(conf) || strcmp(conf->valuestring, def->name) != 0) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "confirmation_required",
                              "confirm must equal the command name");
        }
    }

    uint32_t hold_ms = 0;
    cJSON *params = cJSON_GetObjectItem(json, "params");
    if (params && cJSON_IsObject(params)) {
        cJSON *hold = cJSON_GetObjectItem(params, "hold_ms");
        if (hold) {
            if (!cJSON_IsNumber(hold) || hold->valuedouble < 1000 ||
                hold->valuedouble > 15000) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_params",
                                  "hold_ms must be 1000..15000");
            }
            hold_ms = (uint32_t)hold->valuedouble;
        }
    }
    cJSON_Delete(json);

    pc_cmd_slot_t slot;
    uint32_t retry_ms = 0;
    const char *reason = NULL;
    pc_submit_req_t sreq = {
        .id = def->id,
        .request_id = request_id[0] ? request_id : NULL,
        .hold_ms = hold_ms,
        .out_slot = &slot,
        .out_retry_ms = &retry_ms,
        .out_reason = &reason,
    };

    pc_submit_status_t st = pc_controller_submit(&sreq);
    switch (st) {
    case PC_SUBMIT_OK: {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "request_id", slot.request_id);
        cJSON_AddStringToObject(resp, "state", pc_cmd_state_str(slot.state));
        return send_json_status(req, "202 Accepted", resp);
    }
    case PC_SUBMIT_REPLAY: {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddBoolToObject(resp, "replay", true);
        cJSON_AddItemToObject(resp, "command", slot_to_json(&slot));
        return send_json_status(req, NULL, resp);
    }
    case PC_SUBMIT_BUSY: {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "busy");
        cJSON_AddStringToObject(resp, "active_request_id", slot.request_id);
        const pc_cmd_def_t *adef = pc_cmd_def(slot.id);
        cJSON_AddStringToObject(resp, "active_command", adef ? adef->name : "?");
        cJSON_AddStringToObject(resp, "active_state", pc_cmd_state_str(slot.state));
        return send_json_status(req, "409 Conflict", resp);
    }
    case PC_SUBMIT_COOLDOWN: {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "cooldown");
        cJSON_AddNumberToObject(resp, "retry_after_ms", retry_ms);
        return send_json_status(req, "409 Conflict", resp);
    }
    case PC_SUBMIT_DISABLED:
        return send_error(req, "403 Forbidden", "function_disabled",
                          "enable it in the device Settings UI");
    case PC_SUBMIT_GPIO_UNCONFIGURED:
        return send_error(req, "403 Forbidden", "gpio_not_configured",
                          reason ? reason : "map a GPIO in the device Settings UI");
    case PC_SUBMIT_UNAVAILABLE:
        return send_error(req, "503 Service Unavailable", "unavailable",
                          reason ? reason : "route unavailable");
    default:
        return send_error(req, "500 Internal Server Error", "internal_error", NULL);
    }
}

/* ---------- GET/POST /api/v1/commands/{id}[/cancel] ---------- */

static const char *strip_prefix(const char *uri, const char *prefix) {
    size_t n = strlen(prefix);
    return (strncmp(uri, prefix, n) == 0) ? uri + n : NULL;
}

static esp_err_t handler_get_command_by_id(httpd_req_t *req) {
    const char *rest = strip_prefix(req->uri, "/api/v1/commands/");
    if (!rest || !rest[0] || !id_is_valid(rest)) {
        return send_error(req, "400 Bad Request", "invalid_request_id", NULL);
    }
    pc_cmd_slot_t slot;
    if (!pc_controller_get(rest, &slot)) {
        return send_error(req, "404 Not Found", "not_found", NULL);
    }
    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddItemToObject(json, "command", slot_to_json(&slot));
    return send_json_status(req, NULL, json);
}

/* ---------- configuration endpoints (device Settings UI) ---------- */

static cJSON *config_to_json(void) {
    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);

    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    cJSON_AddBoolToObject(json, "ok", true);

    cJSON *funcs = cJSON_AddObjectToObject(json, "functions");
    for (int i = 0; i < PC_CMD_COUNT; i++) {
        const pc_cmd_def_t *def = pc_cmd_def(i);
        if (def) {
            cJSON_AddBoolToObject(funcs, def->name,
                                  pc_cmd_function_enabled(&cfg, def->id));
        }
    }

    cJSON *gpios = cJSON_AddObjectToObject(json, "gpios");
    for (int i = 0; i < PC_CMD_COUNT; i++) {
        const pc_cmd_def_t *def = pc_cmd_def(i);
        if (!def) continue;
        int slot = pc_cmd_gpio_slot(def->id);
        if (slot < 0) continue;
        if (cfg.gpio[slot] >= 0) cJSON_AddNumberToObject(gpios, def->name, cfg.gpio[slot]);
        else cJSON_AddNullToObject(gpios, def->name);
    }

    /* Active level per hardware function ("high" = drive high to press). */
    cJSON *levels = cJSON_AddObjectToObject(json, "gpio_levels");
    for (int i = 0; i < PC_CMD_COUNT; i++) {
        const pc_cmd_def_t *def = pc_cmd_def(i);
        if (!def) continue;
        int slot = pc_cmd_gpio_slot(def->id);
        if (slot < 0) continue;
        cJSON_AddStringToObject(levels, def->name,
                                cfg.level[slot] == PC_CFG_LEVEL_LOW ? "low" : "high");
    }

    cJSON_AddNumberToObject(json, "short_press_ms", cfg.short_press_ms);
    cJSON_AddNumberToObject(json, "long_press_ms", cfg.long_press_ms);
    cJSON_AddNumberToObject(json, "short_press_min_ms", PC_CFG_SHORT_PRESS_MIN);
    cJSON_AddNumberToObject(json, "short_press_max_ms", PC_CFG_SHORT_PRESS_MAX);
    cJSON_AddNumberToObject(json, "long_press_min_ms", PC_CFG_LONG_PRESS_MIN);
    cJSON_AddNumberToObject(json, "long_press_max_ms", PC_CFG_LONG_PRESS_MAX);

    /* Relay channels (full list - the Settings page edits every slot). */
    cJSON *relays = relays_to_json(true);
    if (relays) cJSON_AddItemToObject(json, "relays", relays);

    cJSON *allowed = cJSON_AddArrayToObject(json, "allowed_gpios");
    size_t n = 0;
    const int *pins = pc_config_allowed_gpios(&n);
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(allowed, cJSON_CreateNumber(pins[i]));
    }

    return json;
}

static esp_err_t handler_get_config(httpd_req_t *req) {
    cJSON *json = config_to_json();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    return send_json_status(req, NULL, json);
}

static esp_err_t handler_settings_ui(httpd_req_t *req) {
    return ml_ui_serve_page(req, "<title>Control Deck - Settings</title>",
                        PCCTL_SETTINGS_MARKUP, PCCTL_SETTINGS_SCRIPT);
}

/* ---------- admin login + API-token management (/pc/security) ---------- */

static bool session_authorized(httpd_req_t *req) {
    char sid[40];
    size_t len = sizeof(sid);
    if (httpd_req_get_cookie_val(req, "pcctl_sess", sid, &len) != ESP_OK) return false;
    return pc_auth_session_valid(sid);
}

static esp_err_t handler_security_ui(httpd_req_t *req) {
    return ml_ui_serve_page(req, "<title>Control Deck - Security</title>",
                        PCCTL_SECURITY_MARKUP, PCCTL_SECURITY_SCRIPT);
}

static esp_err_t handler_auth_status(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    char user[PC_AUTH_USER_MAX + 1] = "";
    char tok[PC_AUTH_TOKEN_MAX + 1];
    pc_auth_username(user, sizeof(user));
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddBoolToObject(json, "logged_in", session_authorized(req));
    cJSON_AddStringToObject(json, "username", user);
    cJSON_AddBoolToObject(json, "api_token_set", pc_auth_api_token(tok, sizeof(tok)));
    cJSON_AddBoolToObject(json, "api_token_custom", pc_auth_api_token_overridden());
    return send_json_status(req, NULL, json);
}

static esp_err_t handler_auth_login(httpd_req_t *req) {
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required", NULL);
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    memset(body, 0, strlen(body));
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    cJSON *u = cJSON_GetObjectItem(json, "username");
    cJSON *p = cJSON_GetObjectItem(json, "password");
    bool ok = cJSON_IsString(u) && cJSON_IsString(p) &&
              pc_auth_verify(u->valuestring, p->valuestring);
    if (p && cJSON_IsString(p)) memset(p->valuestring, 0, strlen(p->valuestring));
    cJSON_Delete(json);

    if (!ok) return send_error(req, "401 Unauthorized", "invalid_credentials", NULL);

    char sid[40];
    if (!pc_auth_session_create(sid, sizeof(sid))) {
        return send_error(req, "500 Internal Server Error", "session_error", NULL);
    }
    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "pcctl_sess=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict",
             sid, PC_AUTH_SESSION_TTL_S);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "logged_in", true);
    return send_json_status(req, NULL, resp);
}

static esp_err_t handler_auth_logout(httpd_req_t *req) {
    char sid[40];
    size_t len = sizeof(sid);
    if (httpd_req_get_cookie_val(req, "pcctl_sess", sid, &len) == ESP_OK) {
        pc_auth_session_destroy(sid);
    }
    httpd_resp_set_hdr(req, "Set-Cookie",
                       "pcctl_sess=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "logged_in", false);
    return send_json_status(req, NULL, resp);
}

static esp_err_t handler_auth_credentials(httpd_req_t *req) {
    if (!session_authorized(req)) {
        return send_error(req, "401 Unauthorized", "login_required",
                          "log in at /pc/security");
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required", NULL);
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    memset(body, 0, strlen(body));
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    cJSON *cur = cJSON_GetObjectItem(json, "current_password");
    cJSON *u = cJSON_GetObjectItem(json, "username");
    cJSON *p = cJSON_GetObjectItem(json, "new_password");
    const char *cur_pw = (cur && cJSON_IsString(cur)) ? cur->valuestring : NULL;
    const char *new_u = (u && cJSON_IsString(u)) ? u->valuestring : NULL;
    const char *new_p = (p && cJSON_IsString(p)) ? p->valuestring : NULL;

    esp_err_t err = pc_auth_change_credentials(cur_pw, new_u, new_p);
    if (cur && cJSON_IsString(cur)) memset(cur->valuestring, 0, strlen(cur->valuestring));
    if (p && cJSON_IsString(p)) memset(p->valuestring, 0, strlen(p->valuestring));
    cJSON_Delete(json);

    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "403 Forbidden", "wrong_password",
                          "the current password is incorrect");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "invalid_credentials",
                          "username 1-32 chars (no spaces); password 1-64 printable ASCII");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          esp_err_to_name(err));
    }
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    return send_json_status(req, NULL, resp);
}

static esp_err_t handler_get_api_token(httpd_req_t *req) {
    if (!session_authorized(req)) {
        return send_error(req, "401 Unauthorized", "login_required",
                          "log in at /pc/security");
    }
    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    char tok[PC_AUTH_TOKEN_MAX + 1];
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddBoolToObject(json, "set", pc_auth_api_token(tok, sizeof(tok)));
    cJSON_AddBoolToObject(json, "custom", pc_auth_api_token_overridden());
    return send_json_status(req, NULL, json);
}

static esp_err_t handler_post_api_token(httpd_req_t *req) {
    if (!session_authorized(req)) {
        return send_error(req, "401 Unauthorized", "login_required",
                          "log in at /pc/security");
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required", NULL);
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    cJSON *gen = cJSON_GetObjectItem(json, "generate");
    cJSON *clear = cJSON_GetObjectItem(json, "clear");
    cJSON *tok = cJSON_GetObjectItem(json, "token");

    char newtok[PC_AUTH_TOKEN_MAX + 1] = "";
    bool show = false;
    esp_err_t err;
    if (gen && cJSON_IsTrue(gen)) {
        err = pc_auth_generate_api_token(newtok, sizeof(newtok));
        show = true;
    } else if (clear && cJSON_IsTrue(clear)) {
        err = pc_auth_set_api_token("");
    } else if (tok && cJSON_IsString(tok)) {
        err = pc_auth_set_api_token(tok->valuestring);
        snprintf(newtok, sizeof(newtok), "%s", tok->valuestring);
        show = true;
    } else {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "invalid_body",
                          "provide {generate:true}, {token}, or {clear:true}");
    }
    cJSON_Delete(json);

    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "invalid_token",
                          "4-96 printable chars, no spaces or quotes");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          esp_err_to_name(err));
    }
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    char eff[PC_AUTH_TOKEN_MAX + 1];
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "set", pc_auth_api_token(eff, sizeof(eff)));
    cJSON_AddBoolToObject(resp, "custom", pc_auth_api_token_overridden());
    if (show) cJSON_AddStringToObject(resp, "token", newtok);
    memset(newtok, 0, sizeof(newtok));
    return send_json_status(req, NULL, resp);
}


/* POST /api/v1/config - partial update of the runtime configuration.
 * Validated + persisted via pc_config_apply (no partial writes); the agent
 * bearer token has its own endpoint and is never echoed. */
static esp_err_t handler_post_config(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        const char *detail =
            (error && strcmp(error, "api_token_not_configured") == 0)
                ? "set CONFIG_PCCTL_API_TOKEN (browser token dialog)"
                : "send X-PC-Token or Authorization: Bearer";
        return send_error(req, status, error, detail);
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required", NULL);
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    pc_config_patch_t patch;
    memset(&patch, 0, sizeof(patch));
    cJSON *item;

    cJSON *funcs = cJSON_GetObjectItem(json, "functions");
    if (funcs && cJSON_IsObject(funcs)) {
        pc_config_snapshot_t cur;
        pc_config_get(&cur);
        uint32_t mask = cur.funcs_mask;
        cJSON *f = NULL;
        cJSON_ArrayForEach(f, funcs) {
            const pc_cmd_def_t *def = pc_cmd_lookup(f->string);
            if (!def || !cJSON_IsBool(f)) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "functions");
            }
            if (cJSON_IsTrue(f)) mask |= (1u << def->id);
            else mask &= ~(1u << def->id);
        }
        patch.set_funcs = true;
        patch.funcs_mask = mask;
    }

    cJSON *gpios = cJSON_GetObjectItem(json, "gpios");
    if (gpios && cJSON_IsObject(gpios)) {
        cJSON *g = NULL;
        cJSON_ArrayForEach(g, gpios) {
            const pc_cmd_def_t *def = pc_cmd_lookup(g->string);
            int slot = pc_cmd_gpio_slot(def ? def->id : PC_CMD_COUNT);
            if (!def || slot < 0) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "gpios");
            }
            int pin;
            if (cJSON_IsNull(g)) pin = -1;                 /* NC */
            else if (cJSON_IsNumber(g)) pin = (int)g->valuedouble;
            else {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "gpios");
            }
            patch.set_gpio[slot] = true;
            patch.gpio[slot] = pin;
        }
    }

    /* Active level per hardware function: "high" | "low" (string, or
     * boolean true = high / false = low for convenience). */
    cJSON *levels = cJSON_GetObjectItem(json, "gpio_levels");
    if (levels && cJSON_IsObject(levels)) {
        cJSON *lv = NULL;
        cJSON_ArrayForEach(lv, levels) {
            const pc_cmd_def_t *def = pc_cmd_lookup(lv->string);
            int slot = pc_cmd_gpio_slot(def ? def->id : PC_CMD_COUNT);
            if (!def || slot < 0) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "gpio_levels");
            }
            if (cJSON_IsTrue(lv)) {
                patch.level[slot] = PC_CFG_LEVEL_HIGH;
            } else if (cJSON_IsFalse(lv)) {
                patch.level[slot] = PC_CFG_LEVEL_LOW;
            } else if (cJSON_IsString(lv) && strcmp(lv->valuestring, "high") == 0) {
                patch.level[slot] = PC_CFG_LEVEL_HIGH;
            } else if (cJSON_IsString(lv) && strcmp(lv->valuestring, "low") == 0) {
                patch.level[slot] = PC_CFG_LEVEL_LOW;
            } else {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "gpio_levels");
            }
            patch.set_level[slot] = true;
        }
    }

    bool have_short = false, have_long = false;
    if ((item = cJSON_GetObjectItem(json, "short_press_ms"))) {
        if (!cJSON_IsNumber(item)) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_field", "short_press_ms");
        }
        patch.set_timings = true;
        patch.short_ms = (int)item->valuedouble;
        have_short = true;
    }
    if ((item = cJSON_GetObjectItem(json, "long_press_ms"))) {
        if (!cJSON_IsNumber(item)) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_field", "long_press_ms");
        }
        patch.set_timings = true;
        patch.long_ms = (int)item->valuedouble;
        have_long = true;
    }
    if (patch.set_timings && (!have_short || !have_long)) {
        pc_config_snapshot_t cur2;
        pc_config_get(&cur2);
        if (!have_short) patch.short_ms = cur2.short_press_ms;
        if (!have_long) patch.long_ms = cur2.long_press_ms;
    }

    esp_err_t err = pc_config_apply(&patch);
    cJSON_Delete(json);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "gpio_conflict",
                          "PWR_RESET must not share a pin with a PWR_SW function, "
                          "and functions sharing a pin must use the same active level, "
                          "and no pin may be used by a relay channel");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "invalid_config",
                          "check field values (pin allowlist, ranges)");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          esp_err_to_name(err));
    }

    cJSON *resp = config_to_json();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    return send_json_status(req, NULL, resp);
}

/* ---------- GET /api/v1/relays ---------- */

static esp_err_t handler_get_relays(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON *relays = relays_to_json(true);
    if (!relays) {
        cJSON_Delete(json);
        return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    }
    cJSON_AddItemToObject(json, "relays", relays);
    size_t count = 0;
    const int *pins = pc_config_allowed_gpios(&count);
    cJSON *allowed = cJSON_AddArrayToObject(json, "allowed_gpios");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(allowed, cJSON_CreateNumber(pins[i]));
    }
    return send_json_status(req, NULL, json);
}

/* ---------- POST /api/v1/relays (explicit ON/OFF) ---------- */

static esp_err_t handler_post_relays(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        const char *detail =
            (error && strcmp(error, "api_token_not_configured") == 0)
                ? "set CONFIG_PCCTL_API_TOKEN (sdkconfig.credentials or menuconfig)"
                : "send X-PC-Token or Authorization: Bearer";
        return send_error(req, status, error, detail);
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required",
                          "use Content-Type: application/json");
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    cJSON *ch_item = cJSON_GetObjectItem(json, "channel");
    cJSON *st_item = cJSON_GetObjectItem(json, "state");
    if (!ch_item || !cJSON_IsNumber(ch_item)) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "missing_channel",
                          "channel is required (0-based number)");
    }
    int id = (int)ch_item->valuedouble;
    if (!st_item || !cJSON_IsString(st_item) ||
        (strcmp(st_item->valuestring, "on") != 0 &&
         strcmp(st_item->valuestring, "off") != 0)) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "invalid_state",
                          "state must be \"on\" or \"off\" (never a toggle)");
    }
    bool on = strcmp(st_item->valuestring, "on") == 0;
    cJSON_Delete(json);

    /* static: handlers run on the shared httpd task; keeps the multi-KB
     * snapshot off its 6 KB stack (same rationale as the other statics) */
    static pc_relay_snapshot_t snap;
    pc_relay_get(&snap);
    if (id < 0 || id >= snap.channel_count) {
        return send_error(req, "404 Not Found", "unknown_channel", NULL);
    }
    const char *reason = NULL;
    if (!pc_relay_channel_available(&snap, (uint8_t)id, &reason)) {
        return send_error(req, "403 Forbidden",
                          snap.channels[id].enabled ? "gpio_not_configured"
                                                    : "channel_disabled",
                          reason);
    }

    bool changed = (snap.channels[id].state != on);
    esp_err_t err = pc_relay_set_state((uint8_t)id, on);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "404 Not Found", "unknown_channel", NULL);
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "state_conflict",
                          "the channel configuration changed - reload the page");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "hardware_failure",
                          esp_err_to_name(err));
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "channel", id);
    cJSON_AddStringToObject(resp, "state", on ? "on" : "off");
    cJSON_AddBoolToObject(resp, "changed", changed);
    cJSON_AddStringToObject(resp, "note",
                            "command issued - the relay contact state is not sensed");
    ESP_LOGI(TAG, "relay %d -> %s commanded", id, on ? "ON" : "OFF");
    return send_json_status(req, NULL, resp);
}

/* Delete one relay channel: the channels after it shift up by one, the tail
 * slot is cleared and the channel count shrinks. Runs through the very same
 * config-patch path, so every rule (pin allowlist, uniqueness, pc-control
 * overlap, safe-start all-OFF apply, single NVS write) is enforced. */
static esp_err_t delete_relay_channel(httpd_req_t *req, int id) {
    /* static: multi-KB snapshot/patch; single serialized httpd task */
    static pc_relay_snapshot_t snap;
    static pc_relay_config_patch_t patch;
    pc_relay_get(&snap);
    if (id < 0 || id >= snap.channel_count) {
        return send_error(req, "404 Not Found", "unknown_channel", NULL);
    }

    memset(&patch, 0, sizeof(patch));
    patch.set_channel_count = true;
    patch.channel_count = (uint8_t)(snap.channel_count - 1);
    for (int i = id; i < snap.channel_count - 1; i++) {
        patch.set_channels[i] = true;
        pc_relay_channel_patch_t *p = &patch.channels[i];
        const pc_relay_channel_t *src = &snap.channels[i + 1];
        snprintf(p->name, sizeof(p->name), "%s", src->name);
        p->set_name = true;
        p->enabled = src->enabled;
        p->set_enabled = true;
        p->gpio = src->gpio;              /* -1 = NC */
        p->set_gpio = true;
        p->level = src->active_level;
        p->set_level = true;
        p->startup = src->startup_policy;
        p->set_startup = true;
    }
    /* The tail slot must be explicitly cleared so the reduction check passes
     * (the firmware never silently drops a GPIO mapping otherwise). */
    patch.set_channels[snap.channel_count - 1] = true;
    pc_relay_channel_patch_t *tail = &patch.channels[snap.channel_count - 1];
    memset(tail, 0, sizeof(*tail));
    tail->set_name = true;               /* empty name */
    tail->set_enabled = true;            /* false */
    tail->set_gpio = true;
    tail->gpio = -1;                     /* NC */
    tail->set_level = true;
    tail->level = PC_CFG_LEVEL_HIGH;
    tail->set_startup = true;
    tail->startup = PC_RELAY_STARTUP_OFF;

    esp_err_t err = pc_relay_config_apply(&patch);
    if (err == ESP_ERR_NOT_ALLOWED) {
        return send_error(req, "409 Conflict", "unsafe_reduction",
                          "a channel being dropped still has a GPIO mapping or is enabled");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "gpio_conflict", NULL);
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "invalid_config", NULL);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          esp_err_to_name(err));
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNumberToObject(resp, "deleted", id);
    cJSON *relays = relays_to_json(true);
    if (relays) cJSON_AddItemToObject(resp, "relays", relays);
    ESP_LOGI(TAG, "relay channel %d deleted (%u left, all driven OFF)",
             id, (unsigned)(snap.channel_count - 1));
    return send_json_status(req, NULL, resp);
}

/* ---------- POST /api/v1/relays/config ---------- */

static esp_err_t handler_post_relays_config(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        const char *detail =
            (error && strcmp(error, "api_token_not_configured") == 0)
                ? "set CONFIG_PCCTL_API_TOKEN (browser token dialog)"
                : "send X-PC-Token or Authorization: Bearer";
        return send_error(req, status, error, detail);
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required", NULL);
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) return send_error(req, "400 Bad Request", "invalid_json", NULL);

    /* {"delete_channel": N} removes one channel and shifts the following
     * channels up (standalone; combining with other config ops is rejected
     * so the two intents can never be ambiguous). */
    cJSON *del = cJSON_GetObjectItem(json, "delete_channel");
    if (del) {
        if (!cJSON_IsNumber(del) ||
            cJSON_GetObjectItem(json, "channel_count") ||
            cJSON_GetObjectItem(json, "channels")) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_field",
                              "delete_channel must be used alone");
        }
        int kid = (int)del->valuedouble;
        cJSON_Delete(json);
        return delete_relay_channel(req, kid);
    }

    /* static: the patch is ~1 KB; keeps the httpd stack small (single
     * serialized httpd task - same rationale as the snapshot statics) */
    static pc_relay_config_patch_t patch;
    memset(&patch, 0, sizeof(patch));

    cJSON *count = cJSON_GetObjectItem(json, "channel_count");
    if (count) {
        if (!cJSON_IsNumber(count) || count->valuedouble < 0 ||
            count->valuedouble > CONFIG_PCCTL_RELAY_MAX_CHANNELS) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_field", "channel_count");
        }
        patch.set_channel_count = true;
        patch.channel_count = (uint8_t)count->valuedouble;
    }

    cJSON *channels = cJSON_GetObjectItem(json, "channels");
    if (channels) {
        if (!cJSON_IsArray(channels)) {
            cJSON_Delete(json);
            return send_error(req, "400 Bad Request", "invalid_field", "channels");
        }
        cJSON *c = NULL;
        cJSON_ArrayForEach(c, channels) {
            cJSON *id_item = cJSON_IsObject(c) ? cJSON_GetObjectItem(c, "id") : NULL;
            if (!id_item || !cJSON_IsNumber(id_item)) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field", "channels.id");
            }
            int id = (int)id_item->valuedouble;
            if (id < 0 || id >= CONFIG_PCCTL_RELAY_MAX_CHANNELS || patch.set_channels[id]) {
                cJSON_Delete(json);
                return send_error(req, "400 Bad Request", "invalid_field",
                                  "channels.id (0..max-1, unique)");
            }
            patch.set_channels[id] = true;
            pc_relay_channel_patch_t *p = &patch.channels[id];
            cJSON *f;
            if ((f = cJSON_GetObjectItem(c, "name")) != NULL) {
                if (!cJSON_IsString(f) || strlen(f->valuestring) > PC_RELAY_NAME_MAX) {
                    cJSON_Delete(json);
                    return send_error(req, "400 Bad Request", "invalid_field",
                                      "channels.name (max 32 bytes, printable)");
                }
                snprintf(p->name, sizeof(p->name), "%s", f->valuestring);
                p->set_name = true;
            }
            if ((f = cJSON_GetObjectItem(c, "enabled")) != NULL) {
                if (!cJSON_IsBool(f)) {
                    cJSON_Delete(json);
                    return send_error(req, "400 Bad Request", "invalid_field", "channels.enabled");
                }
                p->enabled = cJSON_IsTrue(f);
                p->set_enabled = true;
            }
            if ((f = cJSON_GetObjectItem(c, "gpio")) != NULL) {
                if (cJSON_IsNull(f)) {
                    p->gpio = -1;                       /* NC */
                } else if (cJSON_IsNumber(f)) {
                    p->gpio = (int)f->valuedouble;
                } else {
                    cJSON_Delete(json);
                    return send_error(req, "400 Bad Request", "invalid_field", "channels.gpio");
                }
                p->set_gpio = true;
            }
            if ((f = cJSON_GetObjectItem(c, "active_level")) != NULL) {
                if (cJSON_IsTrue(f)) {
                    p->level = PC_CFG_LEVEL_HIGH;
                } else if (cJSON_IsFalse(f)) {
                    p->level = PC_CFG_LEVEL_LOW;
                } else if (cJSON_IsString(f) && strcmp(f->valuestring, "high") == 0) {
                    p->level = PC_CFG_LEVEL_HIGH;
                } else if (cJSON_IsString(f) && strcmp(f->valuestring, "low") == 0) {
                    p->level = PC_CFG_LEVEL_LOW;
                } else {
                    cJSON_Delete(json);
                    return send_error(req, "400 Bad Request", "invalid_field",
                                      "channels.active_level (\"high\" | \"low\")");
                }
                p->set_level = true;
            }
            if ((f = cJSON_GetObjectItem(c, "startup_policy")) != NULL) {
                if (!cJSON_IsString(f) || strcmp(f->valuestring, "off") != 0) {
                    cJSON_Delete(json);
                    return send_error(req, "400 Bad Request", "invalid_field",
                                      "channels.startup_policy (fixed to \"off\" - "
                                      "energized loads are never restored)");
                }
                p->startup = PC_RELAY_STARTUP_OFF;
                p->set_startup = true;
            }
        }
    }
    cJSON_Delete(json);

    esp_err_t err = pc_relay_config_apply(&patch);
    if (err == ESP_ERR_NOT_ALLOWED) {
        return send_error(req, "409 Conflict", "unsafe_reduction",
                          "a channel being dropped still has a GPIO mapping or is "
                          "enabled - set it to NC + disabled first (no mapping is "
                          "silently removed)");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "gpio_conflict",
                          "each relay channel needs its own pin that is not used "
                          "by another relay channel or a PC-control function");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "invalid_config",
                          "check field values (pin allowlist, name/level ranges)");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          esp_err_to_name(err));
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON *relays = relays_to_json(true);
    if (relays) cJSON_AddItemToObject(resp, "relays", relays);
    ESP_LOGI(TAG, "relay configuration saved (all channels driven OFF)");
    return send_json_status(req, NULL, resp);
}

/* ---------- GET /pc ---------- */

static esp_err_t handler_ui(httpd_req_t *req) {
    return ml_ui_serve_page(req, "<title>Control Deck - PC Control</title>",
                        PCCTL_PAGE_MARKUP, PCCTL_PAGE_SCRIPT);
}

/* ---------- POST /api/v1/hid/login + /api/v1/hid/cancel ---------- */

static esp_err_t handler_post_hid_login(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        return send_error(req, status, error, "send X-PC-Token or Authorization: Bearer");
    }
    if (req->content_len > MAX_BODY_LEN) {
        return send_error(req, "413 Payload Too Large", "body_too_large", NULL);
    }
    if (!content_type_is_json(req)) {
        return send_error(req, "415 Unsupported Media Type", "content_type_required",
                          "use Content-Type: application/json");
    }
    char *body = read_body(req);
    if (!body) return send_error(req, "400 Bad Request", "invalid_body", NULL);
    cJSON *json = cJSON_Parse(body);
    if (!json) {
        pc_hid_wipe(body, strlen(body));
        free(body);
        return send_error(req, "400 Bad Request", "invalid_json", NULL);
    }

    cJSON *pw_item = cJSON_GetObjectItem(json, "password");
    cJSON *enter_item = cJSON_GetObjectItem(json, "press_enter");
    bool press_enter = !enter_item || cJSON_IsTrue(enter_item);
    size_t bad_index = 0;
    pc_hid_submit_result_t st = PC_HID_SUBMIT_BAD_CHAR;
    if (pw_item && cJSON_IsString(pw_item)) {
        st = pc_hid_submit(pw_item->valuestring, press_enter, &bad_index);
        /* wipe the parsed copy immediately - pc_hid owns its own buffer */
        pc_hid_wipe(pw_item->valuestring, strlen(pw_item->valuestring));
    }
    pc_hid_wipe(body, strlen(body));
    free(body);
    cJSON_Delete(json);

    switch (st) {
    case PC_HID_SUBMIT_OK: {
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "state", "queued");
        return send_json_status(req, "202 Accepted", resp);
    }
    case PC_HID_SUBMIT_BUSY:
        return send_error(req, "409 Conflict", "busy",
                          "a HID job is already queued or typing");
    case PC_HID_SUBMIT_NOT_MOUNTED:
        return send_error(req, "503 Service Unavailable", "not_mounted",
                          "the native USB port is not enumerated by a host");
    case PC_HID_SUBMIT_DISABLED:
        return send_error(req, "403 Forbidden", "hid_disabled",
                          "enable CONFIG_PCCTL_ENABLE_HID and rebuild");
    case PC_HID_SUBMIT_TOO_LONG: {
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "too_long");
        cJSON_AddNumberToObject(resp, "max_len", (double)pc_hid_max_len());
        return send_json_status(req, "400 Bad Request", resp);
    }
    default: {
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", false);
        cJSON_AddStringToObject(resp, "error", "unsupported_char");
        cJSON_AddNumberToObject(resp, "position", (double)bad_index);
        cJSON_AddStringToObject(resp, "detail",
                                "printable ASCII only - the character itself is never echoed");
        return send_json_status(req, "400 Bad Request", resp);
    }
    }
}

static esp_err_t handler_post_hid_cancel(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        return send_error(req, status, error, "send X-PC-Token or Authorization: Bearer");
    }
    esp_err_t err = pc_hid_cancel();
    if (err == ESP_OK) {
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddBoolToObject(resp, "cancelled", true);
        return send_json_status(req, NULL, resp);
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        return send_error(req, "403 Forbidden", "hid_disabled",
                          "enable CONFIG_PCCTL_ENABLE_HID and rebuild");
    }
    {
        cJSON *resp = cJSON_CreateObject();
        if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddBoolToObject(resp, "cancelled", false);
        cJSON_AddStringToObject(resp, "state", pc_hid_state_str(pc_hid_get_state()));
        cJSON_AddStringToObject(resp, "detail", "nothing cancellable is pending");
        return send_json_status(req, "409 Conflict", resp);
    }
}

/* ---------- device control ---------- */

/* POST /api/v1/device/reboot - restart the ESP32 firmware.
 *
 * A plain reboot: WiFi credentials, the Tailnet identity, the GPIO mapping,
 * the login and the API token are all kept. Token-gated like every other
 * mutation. The response is sent first and the restart follows after a short
 * delay so the reply reaches the client before the network goes away. */
static esp_err_t handler_post_reboot(httpd_req_t *req) {
    const char *status = NULL, *error = NULL;
    if (!authorize_mutation(req, &status, &error)) {
        return send_error(req, status, error, "send X-PC-Token or Authorization: Bearer");
    }
    cJSON *resp = cJSON_CreateObject();
    if (!resp) return send_error(req, "500 Internal Server Error", "no_memory", NULL);
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "rebooting", true);
    esp_err_t err = send_json_status(req, NULL, resp);

    ESP_LOGW(TAG, "device reboot requested via /api/v1/device/reboot");

    /* Let the HTTP response drain, then restart. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return err;   /* unreachable */
}


/* ---------- registration ---------- */

esp_err_t pc_api_start(microlink_t *ml) {
    if (!ml || !ml->config_httpd) {
        ESP_LOGE(TAG, "config httpd context missing (ML_CONFIG_HTTPD off?)");
        return ESP_ERR_INVALID_STATE;
    }
    httpd_handle_t server = (httpd_handle_t)ml_config_httpd_get_handle(ml->config_httpd);
    if (!server) {
        ESP_LOGE(TAG, "config httpd not started yet");
        return ESP_ERR_INVALID_STATE;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/pc",                .method = HTTP_GET,  .handler = handler_ui,                .user_ctx = ml },
        { .uri = "/pc/settings",       .method = HTTP_GET,  .handler = handler_settings_ui,       .user_ctx = ml },
        { .uri = "/pc/security",       .method = HTTP_GET,  .handler = handler_security_ui,       .user_ctx = ml },
        { .uri = "/api/v1/status",     .method = HTTP_GET,  .handler = handler_status,            .user_ctx = ml },
        { .uri = "/api/v1/commands",   .method = HTTP_GET,  .handler = handler_get_commands,      .user_ctx = ml },
        { .uri = "/api/v1/commands",   .method = HTTP_POST, .handler = handler_post_commands,     .user_ctx = ml },
        { .uri = "/api/v1/commands/*", .method = HTTP_GET,  .handler = handler_get_command_by_id, .user_ctx = ml },
        { .uri = "/api/v1/config",       .method = HTTP_GET,  .handler = handler_get_config,        .user_ctx = ml },
        { .uri = "/api/v1/config",       .method = HTTP_POST, .handler = handler_post_config,       .user_ctx = ml },
        { .uri = "/api/v1/hid/login",    .method = HTTP_POST, .handler = handler_post_hid_login,    .user_ctx = ml },
        { .uri = "/api/v1/hid/cancel",   .method = HTTP_POST, .handler = handler_post_hid_cancel,   .user_ctx = ml },
        { .uri = "/api/v1/auth/status",      .method = HTTP_GET,  .handler = handler_auth_status,      .user_ctx = ml },
        { .uri = "/api/v1/auth/login",       .method = HTTP_POST, .handler = handler_auth_login,       .user_ctx = ml },
        { .uri = "/api/v1/auth/logout",      .method = HTTP_POST, .handler = handler_auth_logout,      .user_ctx = ml },
        { .uri = "/api/v1/auth/credentials", .method = HTTP_POST, .handler = handler_auth_credentials, .user_ctx = ml },
        { .uri = "/api/v1/auth/api-token",   .method = HTTP_GET,  .handler = handler_get_api_token,    .user_ctx = ml },
        { .uri = "/api/v1/auth/api-token",   .method = HTTP_POST, .handler = handler_post_api_token,   .user_ctx = ml },
        { .uri = "/api/v1/device/reboot",    .method = HTTP_POST, .handler = handler_post_reboot,      .user_ctx = ml },
        { .uri = "/api/v1/relays",           .method = HTTP_GET,  .handler = handler_get_relays,       .user_ctx = ml },
        { .uri = "/api/v1/relays",           .method = HTTP_POST, .handler = handler_post_relays,      .user_ctx = ml },
        { .uri = "/api/v1/relays/config",    .method = HTTP_POST, .handler = handler_post_relays_config, .user_ctx = ml },
    };

    int registered = 0;
    const int total = (int)(sizeof(routes) / sizeof(routes[0]));
    for (int i = 0; i < total; i++) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err == ESP_OK) {
            registered++;
        } else {
            ESP_LOGE(TAG, "register %s failed: %s", routes[i].uri, esp_err_to_name(err));
        }
    }

    const char *auth_mode;
    char tok[PC_AUTH_TOKEN_MAX + 1];
    if (pc_auth_api_token(tok, sizeof(tok))) {
        auth_mode = "mutations require X-PC-Token";
    } else {
#ifdef CONFIG_PCCTL_ALLOW_NO_TOKEN
        auth_mode = "mutations OPEN (dev escape hatch enabled!)";
#else
        auth_mode = "mutations disabled (no token configured)";
#endif
    }
    ESP_LOGI(TAG, "PC control routes: %d/%d on port 80 (/pc, /api/v1/*; %s)",
             registered, total, auth_mode);
    return (registered == total) ? ESP_OK : ESP_FAIL;
}

