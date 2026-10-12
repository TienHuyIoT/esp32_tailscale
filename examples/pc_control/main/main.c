/**
 * @file main.c
 * @brief MicroLink v2 PC Control - boot, WiFi provisioning and startup of
 *        the PC-control modules (see pc_controller.h / pc_api.h).
 *
 * Boot flow (derived from examples/basic_connect): NVS WiFi credentials
 * (web-UI provisioned) -> WiFi -> SoftAP setup-mode fallback -> MicroLink
 * (Tailscale) -> UDP echo on :9000 (kept for regression parity) ->
 * pc_controller + pc_api on the shared port-80 web server.
 *
 * The device keeps its NVS-provisioned identity across flashes - never
 * erase the flash unless a full reprovision is intended.
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"

#include "microlink.h"
#include "microlink_internal.h"  /* task handle access (diagnostics) */
#include "ml_config_httpd.h"     /* NVS WiFi override at boot */
#include "ml_prov.h"             /* SoftAP setup mode fallback */
#include "esp_timer.h"

#include "pc_controller.h"
#include "pc_config.h"
#include "pc_api.h"

static const char *TAG = "pc_main";

#define MSG_PORT 9000

/* WiFi credentials - start with Kconfig defaults, NVS may override */
static char wifi_ssid[33]     = CONFIG_ML_WIFI_SSID;
static char wifi_password[65] = CONFIG_ML_WIFI_PASSWORD;

/* Multi-SSID support */
static ml_config_wifi_list_t wifi_list;
static int wifi_list_count = 0;      /* 0 = single SSID mode */
static int current_wifi_idx = 0;
static int wifi_retry_count = 0;
#define WIFI_MAX_RETRIES_PER_SSID 3

#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP
#define PROV_STA_RETRY_MS 60000
static int64_t s_last_prov_retry_us = 0;
static bool s_sta_configured = true;
#endif

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static microlink_t *ml = NULL;
static microlink_udp_socket_t *udp_sock = NULL;
static uint32_t msg_rx_count = 0;

/* UDP RX callback: log + echo (regression parity with basic_connect) */
static void on_udp_rx(microlink_udp_socket_t *sock, uint32_t src_ip, uint16_t src_port,
                      const uint8_t *data, size_t len, void *user_data) {
    msg_rx_count++;
    char ip_str[16];
    microlink_ip_to_str(src_ip, ip_str);

    char msg[256];
    size_t copy_len = (len < sizeof(msg) - 1) ? len : sizeof(msg) - 1;
    memcpy(msg, data, copy_len);
    msg[copy_len] = '\0';
    if (copy_len > 0 && msg[copy_len - 1] == '\n') msg[copy_len - 1] = '\0';

    ESP_LOGI(TAG, "UDP RX #%lu from %s:%u [%d bytes]: \"%s\"",
             (unsigned long)msg_rx_count, ip_str, src_port, (int)len, msg);

    char reply[300];
    int reply_len = snprintf(reply, sizeof(reply), "ECHO: %s", msg);
    esp_err_t err = microlink_udp_send(sock, src_ip, src_port, reply, reply_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "UDP TX echo -> %s:%u", ip_str, src_port);
    } else {
        ESP_LOGW(TAG, "UDP TX echo failed: %d (handshake in progress)", err);
    }
}

static void on_state_change(microlink_t *ml_handle, microlink_state_t state, void *user_data) {
    const char *state_names[] = {
        "IDLE", "WIFI_WAIT", "CONNECTING", "REGISTERING",
        "CONNECTED", "RECONNECTING", "ERROR", "AUTH_FAILED"
    };
    const char *name = (state < sizeof(state_names) / sizeof(state_names[0]))
                       ? state_names[state] : "UNKNOWN";
    ESP_LOGI(TAG, "MicroLink state: %s", name);

    if (state == ML_STATE_CONNECTED) {
        uint32_t ip = microlink_get_vpn_ip(ml_handle);
        char ip_str[16];
        microlink_ip_to_str(ip, ip_str);
        ESP_LOGI(TAG, "Connected! VPN IP: %s - control UI: http://%s/pc", ip_str, ip_str);
    }
}

static void on_peer_update(microlink_t *ml_handle, const microlink_peer_info_t *peer,
                           void *user_data) {
    char ip_str[16];
    microlink_ip_to_str(peer->vpn_ip, ip_str);
    ESP_LOGI(TAG, "Peer: %s (%s) online=%d direct=%d",
             peer->hostname, ip_str, peer->online, peer->direct_path);
}

/* Switch to the next WiFi SSID in the list (round-robin) */
static void wifi_try_next(void) {
    if (wifi_list_count <= 1) {
        esp_wifi_connect();
        return;
    }
    wifi_retry_count++;
    if (wifi_retry_count >= WIFI_MAX_RETRIES_PER_SSID) {
        wifi_retry_count = 0;
        current_wifi_idx = (current_wifi_idx + 1) % wifi_list_count;
    }
    ml_config_wifi_entry_t *e = &wifi_list.entries[current_wifi_idx];
    wifi_config_t wifi_config = {
        .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK },
    };
    strncpy((char *)wifi_config.sta.ssid, e->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, e->pass, sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "WiFi trying #%d/%d: %s (retry %d/%d)",
             current_wifi_idx + 1, wifi_list_count, e->ssid,
             wifi_retry_count + 1, WIFI_MAX_RETRIES_PER_SSID);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP
        /* Setup-mode APSTA transitions re-fire STA_START; skip the auto
         * connect then, and when no network is configured at all. */
        if (!ml_prov_is_active() && s_sta_configured) {
            esp_wifi_connect();
        }
#else
        esp_wifi_connect();
#endif
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d", disc->reason);
#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP
        if (ml_prov_is_active()) {
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_prov_retry_us >= (int64_t)PROV_STA_RETRY_MS * 1000) {
                s_last_prov_retry_us = now_us;
                wifi_try_next();
            }
        } else {
            wifi_try_next();
        }
#else
        wifi_try_next();
#endif
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected to %s, IP: " IPSTR,
                 wifi_list_count > 0 ? wifi_list.entries[current_wifi_idx].ssid : wifi_ssid,
                 IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK },
    };
    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Disable WiFi power save for low-latency WireGuard traffic. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "WiFi init complete (PS=NONE), connecting to %s...", wifi_ssid);
}

void app_main(void) {
    /* Initialize NVS (required for WiFi + MicroLink key storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Runtime configuration (NVS-backed; Kconfig values are the defaults) */
    pc_config_init();

    ESP_LOGI(TAG, "MicroLink v2 PC Control example");
    ESP_LOGI(TAG, "Free heap: %lu bytes (PSRAM: %lu bytes)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Check NVS for saved WiFi credentials (from web config UI) */
    memset(&wifi_list, 0, sizeof(wifi_list));
    wifi_list.active_idx = 0xFF;

    if (ml_config_get_wifi_list(&wifi_list) && wifi_list.count > 1) {
        wifi_list_count = wifi_list.count;
        current_wifi_idx = 0;
        strncpy(wifi_ssid, wifi_list.entries[0].ssid, sizeof(wifi_ssid) - 1);
        strncpy(wifi_password, wifi_list.entries[0].pass, sizeof(wifi_password) - 1);
        ESP_LOGI(TAG, "WiFi multi-SSID: %d networks (first: %s)", wifi_list_count, wifi_ssid);
        for (int i = 0; i < wifi_list_count; i++) {
            ESP_LOGI(TAG, "  WiFi #%d: %s", i + 1, wifi_list.entries[i].ssid);
        }
    } else if (ml_config_get_nvs_wifi(wifi_ssid, sizeof(wifi_ssid),
                                      wifi_password, sizeof(wifi_password))) {
        ESP_LOGI(TAG, "Using NVS WiFi: %s", wifi_ssid);
    } else {
        ESP_LOGI(TAG, "Using Kconfig WiFi: %s", wifi_ssid);
    }

#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP
    s_sta_configured = (wifi_list_count > 0) || (wifi_ssid[0] != '\0');
#endif

    wifi_init();

    /* Wait for WiFi; enter setup mode when nothing is reachable */
#ifdef CONFIG_ML_PROV_ENABLE_SOFTAP
    bool enter_setup = false;
    if (!s_sta_configured) {
        ESP_LOGW(TAG, "No WiFi configured - entering SETUP MODE (SoftAP) immediately");
        enter_setup = true;
    } else {
        EventBits_t wifi_bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                                    pdFALSE, pdTRUE,
                                                    pdMS_TO_TICKS(CONFIG_ML_PROV_STA_TIMEOUT_S * 1000));
        if (!(wifi_bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "No WiFi within %d s - entering SETUP MODE (SoftAP)",
                     CONFIG_ML_PROV_STA_TIMEOUT_S);
            enter_setup = true;
        }
    }
    if (enter_setup) {
        s_last_prov_retry_us = esp_timer_get_time();
        esp_err_t prov_err = ml_prov_start(NULL);
        if (prov_err != ESP_OK) {
            ESP_LOGE(TAG, "ml_prov_start failed (%s) - waiting for WiFi anyway",
                     esp_err_to_name(prov_err));
            xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                pdFALSE, pdTRUE, portMAX_DELAY);
        } else {
            ESP_LOGI(TAG, "Setup mode active - AP: %s - open http://192.168.4.1/",
                     ml_prov_get_ap_ssid() ? ml_prov_get_ap_ssid() : "(unknown)");
            while (!(xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT)) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            ESP_LOGI(TAG, "WiFi connected while in setup mode - resuming normal boot");
            ml_prov_stop();
        }
    }
#else
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
#endif

    /* Initialize MicroLink (NVS-saved values override Kconfig defaults) */
    microlink_config_t config = {
        .auth_key = CONFIG_ML_TAILSCALE_AUTH_KEY,
        .device_name = CONFIG_ML_DEVICE_NAME,
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = CONFIG_ML_MAX_PEERS,
        .wifi_tx_power_dbm = 13,  /* reduced for thermal management */
    };

    ml = microlink_init(&config);
    if (!ml) {
        ESP_LOGE(TAG, "Failed to initialize MicroLink");
        return;
    }

    microlink_set_state_callback(ml, on_state_change, NULL);
    microlink_set_peer_callback(ml, on_peer_update, NULL);

    ESP_ERROR_CHECK(microlink_start(ml));

    /* PC control: controller first (owns the state store), then the API
     * routes on the already-running port-80 server. Both work before the
     * VPN connects; status simply reports the tailnet state. */
    ESP_ERROR_CHECK(pc_controller_start(ml));
    esp_err_t api_err = pc_api_start(ml);
    if (api_err != ESP_OK) {
        ESP_LOGE(TAG, "pc_api_start failed (%s) - UI/API not registered",
                 esp_err_to_name(api_err));
    }
    ESP_LOGI(TAG, "PC control ready (mode=%s) - open http://<vpn-ip>/pc once connected",
             pc_controller_mode_str());

    /* Wait for CONNECTED state before creating the UDP echo socket */
    while (!microlink_is_connected(ml)) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    udp_sock = microlink_udp_create(ml, MSG_PORT);
    if (!udp_sock) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
    } else {
        ESP_LOGI(TAG, "UDP echo listening on port %d", MSG_PORT);
        microlink_udp_set_rx_callback(udp_sock, on_udp_rx, NULL);
    }

    /* Periodic diagnostics */
    uint64_t last_status_ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint64_t now = (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now - last_status_ms >= 30000) {
            last_status_ms = now;
            if (microlink_is_connected(ml)) {
                ESP_LOGI(TAG, "Status: CONNECTED | Peers: %d | Heap: %lu",
                         microlink_get_peer_count(ml),
                         (unsigned long)esp_get_free_heap_size());
            }
        }
    }
}
