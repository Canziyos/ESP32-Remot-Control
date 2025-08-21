#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include <stdio.h>
#include "syscoord.h"
#include "errsrc.h"
#include "monitor.h"
#include "gatt_server.h"

#include "wifi_priv.h"

#define TAG "WIFI"

/* local helper */
static const char *wifi_event_name(int32_t id) {
    switch (id) {
        case WIFI_EVENT_WIFI_READY: return "WIFI_READY";
        case WIFI_EVENT_SCAN_DONE: return "SCAN_DONE";
        case WIFI_EVENT_STA_START: return "STA_START";
        case WIFI_EVENT_STA_STOP: return "STA_STOP";
        case WIFI_EVENT_STA_CONNECTED: return "STA_CONNECTED";
        case WIFI_EVENT_STA_DISCONNECTED: return "STA_DISCONNECTED";
        case WIFI_EVENT_STA_AUTHMODE_CHANGE: return "AUTHMODE_CHANGE";
        default: return "OTHER";
    }
}

/* These are referenced by wifi_api.c event registration */
void got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;

    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));
    char line[64];
    int n = snprintf(line, sizeof(line), "WIFI: GOT IP ip=%s", ip);
    if (n > 0) gatt_server_send_status(line);

    syscoord_on_wifi_state(true);
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    errsrc_set(NULL);
    monitor_on_wifi_error(ES_NONE);

    /* reset backoff + stop any pending reconnect timer */
    wifi_backoff_reset();
    wifi_backoff_stop_timer();

    if (!wifi_tcp_timer_is_active()) {
        ESP_LOGI(TAG, "Scheduling TCP server start in 800 ms …");
        wifi_tcp_timer_schedule_us(800000);
    } else {
        ESP_LOGI(TAG, "TCP server already started or timer not set.");
    }
}

void got_ip_lost(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;

    syscoord_on_wifi_state(false);

    // Only post IP_LOST if we're not already in a specific Wi-Fi failure.
    errsrc_t cur_code = errsrc_get_code();
    if (cur_code != ES_NO_AP &&
        cur_code != ES_AUTH_FAIL &&
        cur_code != ES_ASSOC_EXPIRE &&
        cur_code != ES_4WAY_TIMEOUT &&
        cur_code != ES_BEACON_TO) {
        errsrc_set_enum(ES_IP_LOST);
        monitor_on_wifi_error(ES_IP_LOST);
    }
}

void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    ESP_LOGI(TAG, "Event %s (%ld)", wifi_event_name(id), (long)id);

    if (id == WIFI_EVENT_STA_START) {
        /* Only connect if we have an SSID configured */
        wifi_config_t cur = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK && cur.sta.ssid[0] != '\0') {
            errsrc_set("SCANNING");
            wifi_backoff_schedule();
        } else {
            errsrc_set_enum(ES_NO_CREDS);
            monitor_on_wifi_error(ES_NO_CREDS);
            ESP_LOGW(TAG, "No Wi-Fi credentials set; waiting for provisioning.");
        }
        return;

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_tcp_timer_is_active()) {
            ESP_LOGI(TAG, "Cancelling pending TCP start due to disconnect.");
            wifi_tcp_timer_stop_if_active();
        }
        syscoord_on_wifi_state(false);

        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected (reason=%d) – reconnecting …", d->reason);

        errsrc_t err = ES_DISCONNECTED;
        switch (d->reason) {
            case WIFI_REASON_AUTH_EXPIRE: err = ES_AUTH_EXPIRE;  break;
            case WIFI_REASON_AUTH_FAIL: err = ES_AUTH_FAIL; break;
            case WIFI_REASON_NO_AP_FOUND: err = ES_NO_AP; break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT: err = ES_4WAY_TIMEOUT; break;
            case WIFI_REASON_ASSOC_EXPIRE: err = ES_ASSOC_EXPIRE; break;
            case WIFI_REASON_BEACON_TIMEOUT: err = ES_BEACON_TO; break;
            default: err = ES_DISCONNECTED; break;
        }
        errsrc_set_enum(err);
        monitor_on_wifi_error(err);

        wifi_backoff_schedule();
        return;

    } else if (id == WIFI_EVENT_STA_STOP) {
        syscoord_on_wifi_state(false);
        return;
    }
}
