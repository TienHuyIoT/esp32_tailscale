/**
 * @file ml_prov.c
 * @brief MicroLink SoftAP setup mode (provisioning fallback)
 *
 * Started by the application when it cannot join any known WiFi network:
 *  - setup AP (default 192.168.4.1, DHCP server built in) so a phone/laptop
 *    can connect without knowing the target network
 *  - the config web UI (ml_config_httpd, started with ml == NULL) on port 80
 *  - a captive-portal DNS responder that answers every A query with the AP
 *    IP, so phones auto-open the setup page
 *
 * Reference integration: examples/basic_connect — bounded STA wait, then
 * ml_prov_start(NULL), then a soft-recovery loop that calls ml_prov_stop()
 * if WiFi comes back. The user can also save credentials and press
 * "Save & Restart" in the web UI (device reboots with the new network).
 *
 * Gated by CONFIG_ML_PROV_ENABLE_SOFTAP.
 */

#include "sdkconfig.h"

#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP

#include "ml_prov.h"
#include "ml_config_httpd.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"   /* esp_netif_create_default_wifi_ap / _destroy_ */
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static const char *TAG = "ml_prov";

#define PROV_DNS_PORT        53
#define PROV_DNS_TASK_STACK  3072
#define PROV_DNS_TASK_PRIO   4
#define PROV_DNS_BUF_SIZE    512
#define PROV_DNS_POLL_MS     500    /* select() timeout — also stop() exit latency */

typedef struct {
    volatile bool    active;
    esp_netif_t     *ap_netif;
    ml_config_ctx_t *httpd_ctx;
    TaskHandle_t     dns_task;     /* set at create; not polled after that */
    volatile bool    dns_run;      /* false = DNS task should exit */
    volatile bool    dns_running;  /* true while the DNS task is alive */
    uint8_t          ap_ip[4];     /* AP IPv4 bytes, for DNS answers */
    char             ap_ssid[33];
} ml_prov_state_t;

static ml_prov_state_t s_prov;

/* ============================================================================
 * Captive-portal DNS responder
 *
 * Phones/laptops probe well-known URLs to detect captive portals. Resolving
 * everything to the AP IP makes those probes hit our server, whose catch-all
 * handler then 302-redirects into the setup page.
 * ========================================================================== */

/* Build a DNS reply for one query: header + question are copied verbatim,
 * then one A record pointing at s_prov.ap_ip is appended. Returns the reply
 * length, or -1 if the query is unusable. */
static int prov_dns_build_reply(const uint8_t *q, int qlen, uint8_t *r, int rlen)
{
    if (qlen < 12) return -1;
    if (q[2] & 0x80) return -1;                 /* QR set — already a response */

    /* Walk past QNAME (labels) to the NUL terminator */
    int off = 12;
    while (off < qlen && q[off] != 0) {
        if ((q[off] & 0xC0) == 0xC0) return -1; /* compression in a question: malformed */
        off += 1 + q[off];
    }
    if (off + 5 > qlen) return -1;              /* need NUL + QTYPE + QCLASS */
    off += 5;                                   /* end of question section */

    if (rlen < off + 16) return -1;             /* no room for the answer */

    memcpy(r, q, (size_t)off);

    r[2]  = 0x81;                               /* QR=1, RD copied, RA=1 */
    r[3]  = 0x80;
    r[6]  = 0x00; r[7]  = 0x01;                 /* ANCOUNT = 1 */
    r[8]  = 0x00; r[9]  = 0x00;                 /* NSCOUNT = 0 */
    r[10] = 0x00; r[11] = 0x00;                 /* ARCOUNT = 0 */

    uint8_t *a = r + off;
    a[0]  = 0xC0; a[1]  = 0x0C;                 /* name pointer → QNAME */
    a[2]  = 0x00; a[3]  = 0x01;                 /* TYPE  A     */
    a[4]  = 0x00; a[5]  = 0x01;                 /* CLASS IN    */
    a[6]  = 0x00; a[7]  = 0x00;
    a[8]  = 0x00; a[9]  = 0x3C;                 /* TTL 60 s    */
    a[10] = 0x00; a[11] = 0x04;                 /* RDLENGTH 4  */
    a[12] = s_prov.ap_ip[0];
    a[13] = s_prov.ap_ip[1];
    a[14] = s_prov.ap_ip[2];
    a[15] = s_prov.ap_ip[3];
    return off + 16;
}

/* DNS responder task: answers every query with the AP IP (A record). Exits
 * when s_prov.dns_run clears; clears s_prov.dns_running before dying so
 * ml_prov_stop() can poll it. */
static void prov_dns_task(void *arg)
{
    (void)arg;
    uint8_t *qbuf = malloc(PROV_DNS_BUF_SIZE);
    uint8_t *rbuf = malloc(PROV_DNS_BUF_SIZE);
    int sock = -1;

    if (!qbuf || !rbuf) {
        ESP_LOGW(TAG, "DNS buffer alloc failed — captive portal disabled");
    } else {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGW(TAG, "DNS socket() failed (errno=%d) — captive portal disabled", errno);
        } else {
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            struct sockaddr_in addr = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = htonl(INADDR_ANY),
                .sin_port = htons(PROV_DNS_PORT),
            };
            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                ESP_LOGW(TAG, "DNS bind(:%d) failed (errno=%d) — captive portal disabled",
                         PROV_DNS_PORT, errno);
                close(sock);
                sock = -1;
            }
        }
    }

    if (sock >= 0) {
        ESP_LOGI(TAG, "Captive-portal DNS responder on :%d (answers A queries with %u.%u.%u.%u)",
                 PROV_DNS_PORT, s_prov.ap_ip[0], s_prov.ap_ip[1],
                 s_prov.ap_ip[2], s_prov.ap_ip[3]);

        while (s_prov.dns_run) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = PROV_DNS_POLL_MS * 1000 };

            int sel = select(sock + 1, &rfds, NULL, NULL, &tv);
            if (sel <= 0) continue;   /* timeout — re-check dns_run */

            struct sockaddr_in src;
            socklen_t slen = sizeof(src);
            int n = recvfrom(sock, qbuf, PROV_DNS_BUF_SIZE, 0,
                             (struct sockaddr *)&src, &slen);
            if (n <= 0) continue;

            int rl = prov_dns_build_reply(qbuf, n, rbuf, PROV_DNS_BUF_SIZE);
            if (rl > 0) {
                sendto(sock, rbuf, rl, 0, (struct sockaddr *)&src, slen);
            }
        }
        close(sock);
    }

    free(qbuf);
    free(rbuf);
    s_prov.dns_running = false;   /* ml_prov_stop() polls this */
    vTaskDelete(NULL);
}

/* ============================================================================
 * Lifecycle
 * ========================================================================== */

esp_err_t ml_prov_start(const ml_prov_config_t *cfg)
{
    if (s_prov.active) return ESP_OK;

    /* WiFi must already be initialized by the application */
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi not initialized (%s) — call esp_wifi_init() first",
                 esp_err_to_name(err));
        return err;
    }

    /* Mark active early: the APSTA switch below re-fires WIFI_EVENT_STA_START,
     * and the application's handler uses ml_prov_is_active() to skip its
     * auto-connect (setup mode owns reconnection attempts). */
    s_prov.active = true;

    s_prov.httpd_ctx = ml_config_httpd_init();
    if (!s_prov.httpd_ctx) {
        ESP_LOGE(TAG, "ml_config_httpd_init failed");
        goto fail;
    }

    /* AP netif: default IP 192.168.4.1 + built-in DHCP server */
    s_prov.ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_prov.ap_netif) {
        ESP_LOGE(TAG, "AP netif create failed");
        goto fail;
    }

    /* Cache AP IP bytes for the captive-portal DNS answers */
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_prov.ap_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        s_prov.ap_ip[0] = (uint8_t)(ip_info.ip.addr & 0xFF);
        s_prov.ap_ip[1] = (uint8_t)((ip_info.ip.addr >> 8) & 0xFF);
        s_prov.ap_ip[2] = (uint8_t)((ip_info.ip.addr >> 16) & 0xFF);
        s_prov.ap_ip[3] = (uint8_t)((ip_info.ip.addr >> 24) & 0xFF);
    } else {
        s_prov.ap_ip[0] = 192; s_prov.ap_ip[1] = 168;
        s_prov.ap_ip[2] = 4;   s_prov.ap_ip[3] = 1;
    }

    /* SSID: "<prefix>-a1b2c3" from the last 3 bytes of the SoftAP MAC */
    const char *prefix = (cfg && cfg->ap_ssid_prefix) ? cfg->ap_ssid_prefix
                                                      : CONFIG_ML_PROV_AP_SSID_PREFIX;
    if (!prefix || prefix[0] == '\0') prefix = "MicroLink-Setup";
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_prov.ap_ssid, sizeof(s_prov.ap_ssid), "%s-%02x%02x%02x",
             prefix, mac[3], mac[4], mac[5]);

    /* WiFi mode APSTA — AP for the setup UI, STA kept for scanning */
    if (mode != WIFI_MODE_APSTA && mode != WIFI_MODE_AP) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_mode(APSTA) -> %s; retrying via stop/start",
                     esp_err_to_name(err));
            esp_wifi_stop();
            err = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "set_mode(APSTA) failed: %s", esp_err_to_name(err));
                goto fail;
            }
        }
    }

    /* AP config */
    wifi_config_t ap_cfg = { 0 };
    size_t ssid_len = strlen(s_prov.ap_ssid);
    if (ssid_len > sizeof(ap_cfg.ap.ssid)) ssid_len = sizeof(ap_cfg.ap.ssid);
    memcpy(ap_cfg.ap.ssid, s_prov.ap_ssid, ssid_len);
    ap_cfg.ap.ssid_len = (uint8_t)ssid_len;
    ap_cfg.ap.channel = (uint8_t)((cfg && cfg->channel) ? cfg->channel : CONFIG_ML_PROV_AP_CHANNEL);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.pmf_cfg.required = false;

    const char *pass = (cfg && cfg->ap_password) ? cfg->ap_password
                                                 : CONFIG_ML_PROV_AP_PASSWORD;
    bool use_password = pass && strlen(pass) >= 8;
    if (use_password) {
        strncpy((char *)ap_cfg.ap.password, pass, sizeof(ap_cfg.ap.password) - 1);
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
        if (pass && pass[0] != '\0') {
            ESP_LOGW(TAG, "AP password shorter than 8 chars — starting an OPEN network");
        }
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
        goto fail;
    }

    /* Ensure WiFi is running (no-op when the application already started it) */
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    /* Config web UI without a microlink instance (ml == NULL): binds port 80
     * on all interfaces, serves the setup page on http://<ap-ip>/ */
    err = ml_config_httpd_start(s_prov.httpd_ctx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config httpd start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    /* Captive-portal DNS responder (non-fatal if it fails) */
    s_prov.dns_run = true;
    s_prov.dns_running = true;
    if (xTaskCreate(prov_dns_task, "ml_prov_dns", PROV_DNS_TASK_STACK, NULL,
                    PROV_DNS_TASK_PRIO, &s_prov.dns_task) != pdPASS) {
        s_prov.dns_run = false;
        s_prov.dns_running = false;
        s_prov.dns_task = NULL;
        ESP_LOGW(TAG, "DNS task create failed — captive portal disabled");
    }

    ESP_LOGI(TAG, "SETUP MODE ON — AP \"%s\" (%s), web UI http://%u.%u.%u.%u/",
             s_prov.ap_ssid, use_password ? "WPA2" : "OPEN",
             s_prov.ap_ip[0], s_prov.ap_ip[1], s_prov.ap_ip[2], s_prov.ap_ip[3]);
    return ESP_OK;

fail:
    s_prov.active = false;
    if (s_prov.httpd_ctx) {
        ml_config_httpd_deinit(s_prov.httpd_ctx);
        s_prov.httpd_ctx = NULL;
    }
    if (s_prov.ap_netif) {
        esp_netif_destroy_default_wifi(s_prov.ap_netif);
        s_prov.ap_netif = NULL;
    }
    ESP_LOGE(TAG, "ml_prov_start failed — setup mode not active");
    return ESP_FAIL;
}

void ml_prov_stop(void)
{
    if (!s_prov.active && !s_prov.httpd_ctx && !s_prov.ap_netif) return;

    /* Deactivate first: the mode switch below re-fires STA_START and the
     * application's handler should be allowed to auto-connect again. */
    s_prov.active = false;

    /* Stop the DNS responder (select() poll interval is PROV_DNS_POLL_MS) */
    if (s_prov.dns_running) {
        s_prov.dns_run = false;
        for (int i = 0; i < 30 && s_prov.dns_running; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (s_prov.dns_running) {
            ESP_LOGW(TAG, "DNS task still running after 3s — continuing teardown");
        }
        s_prov.dns_task = NULL;
    }

    /* Stop the web UI — MUST happen before microlink_start(), which brings
     * up its own HTTP server on port 80. */
    if (s_prov.httpd_ctx) {
        ml_config_httpd_deinit(s_prov.httpd_ctx);
        s_prov.httpd_ctx = NULL;
    }

    /* Back to STA-only (stops the AP); the app reconnects on STA_START */
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_APSTA || mode == WIFI_MODE_AP)) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "set_mode(STA) -> %s; retrying via stop/start", esp_err_to_name(err));
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
        }
    }

    s_prov.ap_ssid[0] = '\0';
    ESP_LOGI(TAG, "Setup mode OFF");
}

bool ml_prov_is_active(void)
{
    return s_prov.active;
}

const char *ml_prov_get_ap_ssid(void)
{
    return s_prov.ap_ssid[0] ? s_prov.ap_ssid : NULL;
}

#endif /* CONFIG_ML_PROV_ENABLE_SOFTAP */
