/**
 * @file pc_win_client.c
 * @brief Minimal HTTP/1.1 client for the Windows PC agent over MicroLink TCP.
 *
 * Contract with the agent (we own both ends): small JSON responses,
 * "Content-Length:" always present, "Connection: close". Request/response
 * parsing is intentionally lightweight and assumes the canonical header
 * spelling the agent produces.
 *
 * The bearer token is never logged.
 */

#include "pc_win_client.h"
#include "pc_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "pc_win";

#define RX_BUF_SIZE         4096
#define REQ_BUF_SIZE        768
#define CONNECT_TIMEOUT_MS  5000
#define RECV_SLICE_MS       1500

static microlink_t *s_ml;
static uint32_t s_dest_ip;   /* cached resolved VPN IP (0 = resolve again) */
static char s_cached_host[PC_CFG_HOST_MAX];  /* host the cached IP belongs to */
static bool s_fast_fail;     /* short connect timeout while agent is offline */

#define FAST_FAIL_TIMEOUT_MS 1500

static void result_reset(pc_win_result_t *out) {
    memset(out, 0, sizeof(*out));
    out->http_status = -1;
    out->remaining_s = -1;
}

static uint32_t resolve_agent_ip(const char *host) {
    uint32_t ip = microlink_parse_ip(host);
    if (ip != 0) return ip;                 /* plain tailnet IP configured */
    if (strcmp(s_cached_host, host) != 0) {
        s_dest_ip = 0;                      /* host changed: re-resolve */
    }
    uint32_t fresh = microlink_resolve(s_ml, host);
    if (fresh != 0) {
        s_dest_ip = fresh;
        snprintf(s_cached_host, sizeof(s_cached_host), "%s", host);
    }
    return s_dest_ip;                       /* 0 while unknown */
}

static esp_err_t http_exchange(const char *method, const char *path,
                               const char *body, char *resp, size_t resp_len,
                               int *out_status) {
    *out_status = -1;

    pc_config_snapshot_t cfg;
    pc_config_get(&cfg);
    if (cfg.host[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t ip = resolve_agent_ip(cfg.host);
    if (ip == 0) {
        ESP_LOGW(TAG, "agent host '%s' not in peer table yet", cfg.host);
        return ESP_ERR_NOT_FOUND;
    }

    microlink_tcp_socket_t *sock = microlink_tcp_connect(s_ml, ip, cfg.port,
                                    s_fast_fail ? FAST_FAIL_TIMEOUT_MS
                                                : CONNECT_TIMEOUT_MS);
    if (!sock) {
        s_dest_ip = 0;              /* stale IP? force re-resolve next time */
        return ESP_ERR_INVALID_RESPONSE;
    }

    char token[PC_CFG_TOKEN_MAX];
    pc_config_token(token, sizeof(token));
    char req[REQ_BUF_SIZE];
    int len;
    if (body && body[0]) {
        len = snprintf(req, sizeof(req),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s:%u\r\n"
                       "Authorization: Bearer %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %u\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       method, path, cfg.host, (unsigned)cfg.port,
                       token, (unsigned)strlen(body), body);
    } else {
        len = snprintf(req, sizeof(req),
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s:%u\r\n"
                       "Authorization: Bearer %s\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       method, path, cfg.host, (unsigned)cfg.port,
                       token);
    }
    if (len <= 0 || (size_t)len >= sizeof(req)) {
        microlink_tcp_close(sock);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = microlink_tcp_send(sock, req, (size_t)len);
    if (err != ESP_OK) {
        microlink_tcp_close(sock);
        return err;
    }

    int64_t deadline_us = esp_timer_get_time() +
                          (int64_t)cfg.timeout_ms * 1000;
    size_t total = 0;
    size_t body_start = 0;      /* index just past the header terminator */
    long content_length = -1;
    err = ESP_ERR_TIMEOUT;

    while (total < resp_len - 1) {
        int64_t remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
        if (remaining_ms <= 0) break;
        uint32_t slice = (remaining_ms > RECV_SLICE_MS) ? RECV_SLICE_MS
                                                        : (uint32_t)remaining_ms;
        int n = microlink_tcp_recv(sock, resp + total, resp_len - 1 - total, slice);
        if (n < 0) {
            /* peer closed / read error: complete iff the declared body arrived */
            if (body_start &&
                (content_length < 0 || total - body_start >= (size_t)content_length)) {
                err = ESP_OK;
            } else {
                err = ESP_FAIL;
            }
            break;
        }
        if (n == 0) continue;   /* slice timeout: stay inside the budget */

        total += (size_t)n;
        resp[total] = '\0';

        if (body_start == 0) {
            char *hdr_end = strstr(resp, "\r\n\r\n");
            if (hdr_end) {
                body_start = (size_t)(hdr_end - resp) + 4;
                char *cl = strstr(resp, "Content-Length:");
                if (cl) content_length = strtol(cl + 15, NULL, 10);
                int code = 0;
                if (sscanf(resp, "HTTP/%*s %d", &code) == 1) *out_status = code;
            }
        }
        if (body_start && content_length >= 0 &&
            total - body_start >= (size_t)content_length) {
            err = ESP_OK;
            break;
        }
    }

    microlink_tcp_close(sock);
    return err;
}

/* Parse the agent's JSON reply (the subset the firmware relies on). */
static void parse_agent_json(const char *resp, pc_win_result_t *out) {
    const char *body = strstr(resp, "\r\n\r\n");
    if (!body) return;
    body += 4;
    cJSON *json = cJSON_Parse(body);
    if (!json) {
        snprintf(out->reason, sizeof(out->reason), "invalid JSON from agent");
        return;
    }
    out->agent_ok = cJSON_IsTrue(cJSON_GetObjectItem(json, "ok"));
    cJSON *dr = cJSON_GetObjectItem(json, "dry_run");
    if (dr) out->dry_run = cJSON_IsTrue(dr);
    cJSON *pend = cJSON_GetObjectItem(json, "pending");
    if (pend && cJSON_IsObject(pend)) {
        out->pending = true;
        cJSON *op = cJSON_GetObjectItem(pend, "operation");
        if (op && cJSON_IsString(op)) {
            snprintf(out->operation, sizeof(out->operation), "%s", op->valuestring);
        }
        cJSON *rem = cJSON_GetObjectItem(pend, "remaining_s");
        if (rem && cJSON_IsNumber(rem)) out->remaining_s = (int64_t)rem->valuedouble;
    }
    cJSON *canc = cJSON_GetObjectItem(json, "cancelled");
    if (canc) out->cancelled = cJSON_IsTrue(canc);
    cJSON *reason = cJSON_GetObjectItem(json, "reason");
    if (reason && cJSON_IsString(reason)) {
        snprintf(out->reason, sizeof(out->reason), "%s", reason->valuestring);
    }
    cJSON *errs = cJSON_GetObjectItem(json, "error");
    if (errs && cJSON_IsString(errs)) {
        snprintf(out->error, sizeof(out->error), "%s", errs->valuestring);
    }
    cJSON_Delete(json);
}

static bool do_request(const char *method, const char *path, const char *body,
                       pc_win_result_t *out) {
    static char resp[RX_BUF_SIZE];   /* used from the pc_ctl task only */
    result_reset(out);
    int status = -1;
    esp_err_t err = http_exchange(method, path, body, resp, sizeof(resp), &status);
    out->http_status = status;

    if (err != ESP_OK) {
        pc_config_snapshot_t c2;
        pc_config_get(&c2);
        switch (err) {
        case ESP_ERR_NOT_FOUND:
            snprintf(out->error, sizeof(out->error), "agent host not known in peer table");
            break;
        case ESP_ERR_INVALID_STATE:
            snprintf(out->error, sizeof(out->error), "no agent host configured (Settings)");
            break;
        case ESP_ERR_INVALID_RESPONSE:
            snprintf(out->error, sizeof(out->error), "TCP connect to %s:%u failed",
                     c2.host, (unsigned)c2.port);
            break;
        case ESP_ERR_TIMEOUT:
            snprintf(out->error, sizeof(out->error), "agent did not answer within %lu ms",
                     (unsigned long)c2.timeout_ms);
            break;
        default:
            snprintf(out->error, sizeof(out->error), "agent I/O error (%s)",
                     esp_err_to_name(err));
            break;
        }
        return false;
    }

    parse_agent_json(resp, out);
    out->ok = (out->http_status >= 200 && out->http_status < 300) && out->agent_ok;
    if (!out->ok && out->error[0] == '\0') {
        snprintf(out->error, sizeof(out->error), "agent returned HTTP %d (ok=%s)",
                 out->http_status, out->agent_ok ? "true" : "false");
    }
    return out->ok;
}

esp_err_t pc_win_client_setup(microlink_t *ml) {
    s_ml = ml;
    s_dest_ip = 0;
    s_cached_host[0] = '\0';
    s_fast_fail = false;
    return ESP_OK;
}

void pc_win_client_invalidate(void) {
    s_dest_ip = 0;
    s_cached_host[0] = '\0';
}

esp_err_t pc_win_client_set_fast_fail(bool fast) {
    s_fast_fail = fast;
    return ESP_OK;
}

bool pc_win_client_get_status(pc_win_result_t *out) {
    return do_request("GET", "/api/v1/status", NULL, out);
}

bool pc_win_client_post_shutdown(uint32_t delay_s, pc_win_result_t *out) {
    char body[48];
    snprintf(body, sizeof(body), "{\"delay_s\":%lu}", (unsigned long)delay_s);
    return do_request("POST", "/api/v1/shutdown", body, out);
}

bool pc_win_client_post_restart(uint32_t delay_s, pc_win_result_t *out) {
    char body[48];
    snprintf(body, sizeof(body), "{\"delay_s\":%lu}", (unsigned long)delay_s);
    return do_request("POST", "/api/v1/restart", body, out);
}

bool pc_win_client_post_cancel(pc_win_result_t *out) {
    return do_request("POST", "/api/v1/cancel-shutdown", "{}", out);
}
