#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "tcp_server.h"
#include "syscoord.h"
#include "errsrc.h"
#include "monitor.h"   // event-driven health escalation
#include "lwip/ip4_addr.h"  // ip4addr_ntoa_r
#include "gatt_server.h" // for gatt_server_send_status

static const char *TAG = "WIFI";
static bool ota_verified = false;
static esp_timer_handle_t s_tcp_start_timer = NULL;

/* --- Forward decls for handlers --- */
static void got_ip(void *arg, esp_event_base_t b, int32_t id, void *data);
static void got_ip_lost(void *arg, esp_event_base_t b, int32_t id, void *data);
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data);

/* --- TCP start delay handling --- */
static void tcp_start_timer_cb(void *args) {
    (void)args;
    launch_tcp_server();
    ESP_LOGI(TAG, "TCP server started after Got_IP settled.");
}
static void wifi_create_tcp_timer_once(void) {
    if (s_tcp_start_timer) return;
    const esp_timer_create_args_t args = {
        .callback = &tcp_start_timer_cb,
        .name = "tcp_start_after_ip"
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_tcp_start_timer));
}

/* --- Update Wi-Fi credentials in NVS + reconnect --- */
void wifi_set_credentials(const char *ssid, const char *pwd) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for Wi-Fi credentials");
        return;
    }
    if (!ssid) ssid = "";
    if (!pwd)  pwd  = "";

    esp_err_t e1 = nvs_set_str(h, "ssid", ssid);
    esp_err_t e2 = nvs_set_str(h, "password", pwd);
    esp_err_t ec = nvs_commit(h);
    nvs_close(h);

    if (e1 != ESP_OK || e2 != ESP_OK || ec != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi creds (set: %s/%s, commit: %s)",
                 esp_err_to_name(e1), esp_err_to_name(e2), esp_err_to_name(ec));
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi credentials saved: SSID=%s", ssid);

    /* Apply new config and reconnect */
    ESP_ERROR_CHECK(esp_wifi_disconnect());

    wifi_config_t sta_cfg = (wifi_config_t){0};
    strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char*)sta_cfg.sta.password, pwd, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = strlen(pwd) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    errsrc_set("TRYING");
    ESP_ERROR_CHECK(esp_wifi_connect());
}

/* --- Event name helper --- */
static const char *wifi_event_name(int32_t id) {
    switch (id) {
        case WIFI_EVENT_WIFI_READY:          return "WIFI_READY";
        case WIFI_EVENT_SCAN_DONE:           return "SCAN_DONE";
        case WIFI_EVENT_STA_START:           return "STA_START";
        case WIFI_EVENT_STA_STOP:            return "STA_STOP";
        case WIFI_EVENT_STA_CONNECTED:       return "STA_CONNECTED";
        case WIFI_EVENT_STA_DISCONNECTED:    return "STA_DISCONNECTED";
        case WIFI_EVENT_STA_AUTHMODE_CHANGE: return "AUTHMODE_CHANGE";
        default:                             return "OTHER";
    }
}

// got_ip handler
static void got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;

    char ip[16];
    // no type fights, just print it
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));

    char line[64];
    int n = snprintf(line, sizeof(line), "WIFI: GOT IP ip=%s", ip);
    if (n > 0) gatt_server_send_status(line);

    syscoord_on_wifi_state(true);
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    if (!ota_verified) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        if (running && running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            ESP_LOGI(TAG, "Running from factory image — skipping OTA valid-mark.");
            ota_verified = true;
        } else {
            esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
            if (e == ESP_OK) {
                ESP_LOGI(TAG, "OTA verified - rollback cancelled.");
            } else if (e == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "OTA already valid (no pending-verify).");
            } else {
                ESP_LOGW(TAG, "OTA valid-mark failed: %s", esp_err_to_name(e));
            }
            ota_verified = true;
        }
    }

    errsrc_set(NULL);                    // "NONE"
    monitor_on_wifi_error(ERRSRC_NONE); 

    if (s_tcp_start_timer && !esp_timer_is_active(s_tcp_start_timer)) {
        ESP_LOGI(TAG, "Scheduling TCP server start in 800 ms …");
        ESP_ERROR_CHECK(esp_timer_start_once(s_tcp_start_timer, 800000));
    } else {
        ESP_LOGI(TAG, "TCP server already started or timer not set.");
    }
}


/* --- IP lost --- */
static void got_ip_lost(void *arg, esp_event_base_t b, int32_t id, void *data) {
    (void)arg; (void)b; (void)id; (void)data;
    syscoord_on_wifi_state(false);
    errsrc_set("IP_LOST");
    monitor_on_wifi_error(ERRSRC_IP_LOST);      // NEW
}

/* --- Wi-Fi events --- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    ESP_LOGI(TAG, "Event %s (%ld)", wifi_event_name(id), (long)id);

    if (id == WIFI_EVENT_STA_START) {
        /* Only connect if we have an SSID configured */
        wifi_config_t cur = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK && cur.sta.ssid[0] != '\0') {
            errsrc_set("SCANNING");
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else {
            errsrc_set("NO_CREDS");
            monitor_on_wifi_error(ERRSRC_NO_CREDS);   // NEW
            ESP_LOGW(TAG, "No Wi-Fi credentials set; waiting for provisioning.");
        }
        return;

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Cancel pending TCP start if Wi-Fi dropped before timer fired */
        if (s_tcp_start_timer && esp_timer_is_active(s_tcp_start_timer)) {
            ESP_LOGI(TAG, "Cancelling pending TCP start due to disconnect.");
            ESP_ERROR_CHECK(esp_timer_stop(s_tcp_start_timer));
        }
        syscoord_on_wifi_state(false);

        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected (reason=%d) – reconnecting …", d->reason);

        const char *reason = "DISCONNECTED";
        errsrc_t err = ERRSRC_DISCONNECTED;          // NEW: default mapping
        switch (d->reason) {
            case WIFI_REASON_AUTH_EXPIRE:       reason = "AUTH_EXPIRE";  err = ERRSRC_AUTH_EXPIRE;  break;
            case WIFI_REASON_AUTH_FAIL:         reason = "AUTH_FAIL";    err = ERRSRC_AUTH_FAIL;    break;
            case WIFI_REASON_NO_AP_FOUND:       reason = "NO_AP";        err = ERRSRC_NO_AP;        break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT: reason = "4WAY_TIMEOUT"; err = ERRSRC_4WAY_TIMEOUT; break;
            case WIFI_REASON_ASSOC_EXPIRE:      reason = "ASSOC_EXPIRE"; err = ERRSRC_ASSOC_EXPIRE; break;
            case WIFI_REASON_BEACON_TIMEOUT:    reason = "BEACON_TO";    err = ERRSRC_BEACON_TO;    break;
            default:                                                     err = ERRSRC_DISCONNECTED; break;
        }
        errsrc_set(reason);
        monitor_on_wifi_error(err);                 // NEW: drive escalation

        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;

    } else if (id == WIFI_EVENT_STA_STOP) {
        syscoord_on_wifi_state(false);
        // Optional: treat as transient; don't escalate here.
        return;
    }
}

/* --- Start Wi-Fi --- */
void wifi_start(const char *ssid, const char *pwd) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_create_tcp_timer_once();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_LOST_IP, got_ip_lost, NULL, NULL));

    wifi_config_t sta_cfg = (wifi_config_t){0};

    if (ssid && strlen(ssid) > 0) {
        strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid)-1);
        strncpy((char*)sta_cfg.sta.password, pwd ? pwd : "", sizeof(sta_cfg.sta.password)-1);
    } else {
        /* Load from NVS if present */
        nvs_handle_t h;
        if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
            size_t ssid_len = sizeof(sta_cfg.sta.ssid);
            size_t pwd_len  = sizeof(sta_cfg.sta.password);
            esp_err_t e1 = nvs_get_str(h, "ssid", (char*)sta_cfg.sta.ssid, &ssid_len);
            esp_err_t e2 = nvs_get_str(h, "password", (char*)sta_cfg.sta.password, &pwd_len);
            nvs_close(h);
            if (e1 == ESP_OK) {
                ESP_LOGI(TAG, "Loaded Wi-Fi SSID from NVS");
            } else {
                sta_cfg.sta.ssid[0]     = '\0';
                sta_cfg.sta.password[0] = '\0';
                ESP_LOGW(TAG, "No Wi-Fi SSID in NVS (ERR=%s)", esp_err_to_name(e1));
            }
            if (e2 != ESP_OK) {
                sta_cfg.sta.password[0] = '\0';
            }
        }
    }

    sta_cfg.sta.threshold.authmode =
        strlen((char*)sta_cfg.sta.password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi driver started – waiting for IP …");

    /* If this image was already marked VALID, remember that */
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_VALID) {
        ota_verified = true;
    }
}
