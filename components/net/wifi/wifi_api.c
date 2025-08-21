#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs.h"

#include "app_cfg.h"
#include "wifi.h"          // public API
#include "wifi_priv.h"     // internal helpers
#include "tcp_server.h"    // launch_tcp_server()
#include "errsrc.h"
#include "monitor.h"
#include "syscoord.h"

#define TAG "WIFI"

static esp_timer_handle_t s_tcp_start_timer = NULL;

/* --- TCP start timer plumbing (internal) --- */
static void tcp_start_timer_cb(void *args) {
    (void)args;
    launch_tcp_server();
    ESP_LOGI(TAG, "TCP server started after Got_IP settled.");
}

void wifi_tcp_timer_create_once(void) {
    if (s_tcp_start_timer) return;
    const esp_timer_create_args_t args = {
        .callback = &tcp_start_timer_cb,
        .name = "tcp_start_after_ip"
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_tcp_start_timer));
}

bool wifi_tcp_timer_is_active(void) {
    return s_tcp_start_timer && esp_timer_is_active(s_tcp_start_timer);
}

void wifi_tcp_timer_stop_if_active(void) {
    if (s_tcp_start_timer && esp_timer_is_active(s_tcp_start_timer)) {
        ESP_ERROR_CHECK(esp_timer_stop(s_tcp_start_timer));
    }
}

void wifi_tcp_timer_schedule_us(uint64_t usec) {
    if (!s_tcp_start_timer) return;
    ESP_ERROR_CHECK(esp_timer_start_once(s_tcp_start_timer, usec));
}

/* --- Public API --- */
void wifi_set_credentials(const char *ssid, const char *pwd) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for Wi-Fi credentials");
        return;
    }
    if (!ssid) ssid = "";
    if (!pwd)  pwd  = "";

    esp_err_t e1 = nvs_set_str(h, NVS_KEY_WIFI_SSID, ssid);
    esp_err_t e2 = nvs_set_str(h, NVS_KEY_WIFI_PASSWORD, pwd);
    esp_err_t ec = nvs_commit(h);
    nvs_close(h);

    if (e1 != ESP_OK || e2 != ESP_OK || ec != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save Wi-Fi creds (set: %s/%s, commit: %s)",
                 esp_err_to_name(e1), esp_err_to_name(e2), esp_err_to_name(ec));
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi credentials saved: SSID=%s", ssid);

    /* Apply new config and reconnect */
    esp_err_t de = esp_wifi_disconnect();
    if (de != ESP_OK && de != ESP_ERR_WIFI_NOT_STARTED && de != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect(): %s", esp_err_to_name(de));
    }

    wifi_config_t sta_cfg = (wifi_config_t){0};
    strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char*)sta_cfg.sta.password, pwd, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = strlen(pwd) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    errsrc_set("TRYING");

    /* start a fresh backoff-driven reconnect */
    wifi_backoff_stop_timer();
    wifi_backoff_reset();
    wifi_backoff_schedule();
}


void wifi_start(const char *ssid, const char *pwd) {
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t er = esp_event_loop_create_default();
    if (er != ESP_OK && er != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(er);
    assert(esp_netif_create_default_wifi_sta() && "create_default_wifi_sta failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_tcp_timer_create_once();

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
        if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) == ESP_OK) {
            size_t ssid_len = sizeof(sta_cfg.sta.ssid);
            size_t pwd_len  = sizeof(sta_cfg.sta.password);
            esp_err_t e1 = nvs_get_str(h, NVS_KEY_WIFI_SSID, (char*)sta_cfg.sta.ssid, &ssid_len);
            esp_err_t e2 = nvs_get_str(h, NVS_KEY_WIFI_PASSWORD, (char*)sta_cfg.sta.password, &pwd_len);
            nvs_close(h);

            if (e1 == ESP_OK) ESP_LOGI(TAG, "Loaded Wi-Fi SSID from NVS");
            else {
                sta_cfg.sta.ssid[0]     = '\0';
                sta_cfg.sta.password[0] = '\0';
                ESP_LOGW(TAG, "No Wi-Fi SSID in NVS (ERR=%s)", esp_err_to_name(e1));
            }
            if (e2 != ESP_OK) sta_cfg.sta.password[0] = '\0';
        }
    }

    sta_cfg.sta.threshold.authmode =
        strlen((char*)sta_cfg.sta.password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi driver started – waiting for IP …");
}

+/* handlers are declared in wifi_priv.h and defined in wifi_event.c */