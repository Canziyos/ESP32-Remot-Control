#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include <assert.h>
#include "lwip/inet.h"
#include "esp_event.h"
#include "nvs.h"
#include "tcp_server.h"
#include "syscoord.h"
#include "errsrc.h"
#include "monitor.h"   // event-driven health escalation
#include "gatt_server.h"
#include "app_cfg.h"
#include <stdio.h>

static const char *TAG = "WIFI";
static esp_timer_handle_t s_tcp_start_timer = NULL;
static TimerHandle_t s_reconn_tmr;
static int s_retries = 0;

#define RECONN_BASE_MS WIFI_RECONN_BASE_MS
#define RECONN_MAX_MS  WIFI_RECONN_MAX_MS

/* --- Forward decls for handlers --- */
static void got_ip(void *arg, esp_event_base_t b, int32_t id, void *data);
static void got_ip_lost(void *arg, esp_event_base_t b, int32_t id, void *data);
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data);
static void schedule_reconnect(void);

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
    // CHANGED: make disconnect non-fatal so we don’t abort if Wi-Fi not started yet.
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

    // stop any pending reconnect timer before starting a fresh backoff cycle
    if (s_reconn_tmr) xTimerStop(s_reconn_tmr, 0);

    /* start a fresh backoff-driven reconnect instead of direct connect */
    s_retries = 0;
    schedule_reconnect();
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
    errsrc_set(NULL); 
    (void)arg; (void)base; (void)id;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;

    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ev->ip_info.ip));

    char line[64];
    int n = snprintf(line, sizeof(line), "WIFI: GOT IP ip=%s", ip);
    if (n > 0) gatt_server_send_status(line);

    syscoord_on_wifi_state(true);
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    monitor_on_wifi_error(ES_NONE);

    /* CHANGED: success -> reset backoff and stop any pending reconnect timer */
    s_retries = 0;
    if (s_reconn_tmr) xTimerStop(s_reconn_tmr, 0);

    if (s_tcp_start_timer && !esp_timer_is_active(s_tcp_start_timer)) {
        ESP_LOGI(TAG, "Scheduling TCP server start in 800 ms …");
        ESP_ERROR_CHECK(esp_timer_start_once(s_tcp_start_timer, 800000));
    } else {
        ESP_LOGI(TAG, "TCP server already started or timer not set.");
    }
}


/* --- Start Wi-Fi --- */
void wifi_start(const char *ssid, const char *pwd) {
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t er = esp_event_loop_create_default();
    if (er != ESP_OK && er != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(er);
    }
    assert(esp_netif_create_default_wifi_sta() && "create_default_wifi_sta failed");

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
        if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) == ESP_OK) {
            size_t ssid_len = sizeof(sta_cfg.sta.ssid);
            size_t pwd_len  = sizeof(sta_cfg.sta.password);
            esp_err_t e1 = nvs_get_str(h, NVS_KEY_WIFI_SSID, (char*)sta_cfg.sta.ssid, &ssid_len);
            esp_err_t e2 = nvs_get_str(h, NVS_KEY_WIFI_PASSWORD, (char*)sta_cfg.sta.password, &pwd_len);
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
}

/* --- IP lost --- */
static void got_ip_lost(void *arg, esp_event_base_t base, int32_t id, void *data)
{
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


/* --- Wi-Fi events --- */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base;
    ESP_LOGI(TAG, "Event %s (%ld)", wifi_event_name(id), (long)id);

    if (id == WIFI_EVENT_STA_START) {
        /* Only connect if we have an SSID configured */
        wifi_config_t cur = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK && cur.sta.ssid[0] != '\0') {
            errsrc_set("SCANNING");
            /* CHANGED: use backoff-driven connect */
            schedule_reconnect();
        } else {
            errsrc_set_enum(ES_NO_CREDS);
            monitor_on_wifi_error(ES_NO_CREDS);
            ESP_LOGW(TAG, "No Wi-Fi credentials set; waiting for provisioning.");
        }
        return;

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        /* CHANGED: do NOT reset retries here; do NOT direct-connect. */
        if (s_tcp_start_timer && esp_timer_is_active(s_tcp_start_timer)) {
            ESP_LOGI(TAG, "Cancelling pending TCP start due to disconnect.");
            ESP_ERROR_CHECK(esp_timer_stop(s_tcp_start_timer));
        }
        syscoord_on_wifi_state(false);

        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "Disconnected (reason=%d) – reconnecting …", d->reason);

        errsrc_t err = ES_DISCONNECTED;
        switch (d->reason) {
            case WIFI_REASON_AUTH_EXPIRE: err = ES_AUTH_EXPIRE; break;
            case WIFI_REASON_AUTH_FAIL: err = ES_AUTH_FAIL; break;
            case WIFI_REASON_NO_AP_FOUND: err = ES_NO_AP; break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT: err = ES_4WAY_TIMEOUT; break;
            case WIFI_REASON_ASSOC_EXPIRE: err = ES_ASSOC_EXPIRE; break;
            case WIFI_REASON_BEACON_TIMEOUT: err = ES_BEACON_TO; break;
            default: err = ES_DISCONNECTED; break;
        }
        errsrc_set_enum(err);
        monitor_on_wifi_error(err);

        /* CHANGED: schedule backoff reconnect instead of calling esp_wifi_connect() here */
        schedule_reconnect();
        return;

    } else if (id == WIFI_EVENT_STA_STOP) {
        syscoord_on_wifi_state(false);
        return;
    }
}

static void wifi_reconnect_timer_cb(TimerHandle_t xTimer)
{
    esp_err_t e = esp_wifi_connect();
    if (e == ESP_ERR_WIFI_CONN) {
        // driver is already trying; not fatal
        ESP_LOGW(TAG, "connect(): already connecting; will rely on events.");
    } else if (e != ESP_OK) {
        ESP_LOGE(TAG, "connect(): %s", esp_err_to_name(e));
    } else {
        ESP_LOGI(TAG, "connect(): requested.");
    }
}

static void schedule_reconnect(void)
{
    if (!s_reconn_tmr) {
        s_reconn_tmr = xTimerCreate("wifi.reconn", pdMS_TO_TICKS(1000), pdFALSE, NULL, wifi_reconnect_timer_cb);
    }
    uint32_t delay_ms = RECONN_BASE_MS << (s_retries < 6 ? s_retries : 6); // up to 32x
    if (delay_ms > RECONN_MAX_MS) delay_ms = RECONN_MAX_MS;

    /* Optional jitter to avoid herd reconnects (centrally tuned in app_cfg.h). */
#if WIFI_RECONN_JITTER_PCT > 0
    {
        uint32_t span = (delay_ms * WIFI_RECONN_JITTER_PCT) / 100U; // e.g., 10%
        if (span) {
            uint32_t r   = esp_random();                            // 32-bit RNG
            int32_t  off = (int32_t)(r % span) - (int32_t)(span / 2U);
            int32_t  d2  = (int32_t)delay_ms + off;
            delay_ms = (uint32_t)((d2 < 0) ? 0 : d2);
        }
    }
#endif
    // (re)arm one-shot
    xTimerStop(s_reconn_tmr, 0);
    xTimerChangePeriod(s_reconn_tmr, pdMS_TO_TICKS(delay_ms), 0);
    xTimerStart(s_reconn_tmr, 0);

    ESP_LOGI(TAG, "Will reconnect in %u ms (retry #%d).", (unsigned)delay_ms, s_retries);
    s_retries++;
}

