// components/ble/fallback/fb_gap.c
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_gap_ble_api.h"
#include "fb_priv.h"

static const char *TAG = "BLE.fb.gap";

/* Basic stop (tolerant). */
static void fb_adv_stop(void) {
    if (!s_adv_running) return;
    esp_err_t e = esp_ble_gap_stop_advertising();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "stop advertising: %s", esp_err_to_name(e));
    }
}
void fb_adv_stop_public(void) { fb_adv_stop(); }

/* Start now if allowed (worker context). */
void fb_adv_kick(void) {
    if (!s_lifeboat_enabled) {
        ESP_LOGI(TAG, "lifeboat disabled -> not advertising");
        return;
    }
    if (!s_stack_ready || !s_adv_ready) {
        s_adv_start_deferred = true;
        return;
    }
    if (s_connected) return;

    esp_err_t e = esp_ble_gap_start_advertising(&s_adv_params);
    if (e == ESP_ERR_INVALID_STATE) {
        (void)esp_ble_gap_stop_advertising();
        e = esp_ble_gap_start_advertising(&s_adv_params);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(e));
    }
}

/* ADV watchdog: if idle & lifeboat on, request restart. */
static void adv_watch_cb(TimerHandle_t t) {
    (void)t;
    if (s_lifeboat_enabled && !s_connected && s_adv_ready && s_stack_ready && !s_adv_running) {
        ESP_LOGW(TAG, "ADV watchdog: requesting restart.");
        ble_post(BLE_EVT_ADV_KICK);
    }
}
void fb_watchdog_start_if_needed(void) {
    if (!s_adv_watch) {
        s_adv_watch = xTimerCreate("adv_watch", pdMS_TO_TICKS(2000), pdTRUE, NULL, adv_watch_cb);
        if (s_adv_watch) xTimerStart(s_adv_watch, 0);
    }
}
void fb_watchdog_stop(void) {
    if (s_adv_watch) xTimerStop(s_adv_watch, 0);
}

/* Bit flags for adv/scn payload config */
#define ADV_CFG_FLAG      (1 << 0)
#define SCAN_RSP_CFG_FLAG (1 << 1)

/* GAP callback (registered from fb_core.c) */
void fb_gap_evt(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p) {
    switch (ev) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (p->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_cfg_done |= ADV_CFG_FLAG;
            ESP_LOGI(TAG, "ADV payload configured.");
            ESP_LOGI(TAG, "ADV UUID128 (LSB->MSB): "
                  "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                  s_adv_uuid[0], s_adv_uuid[1], s_adv_uuid[2], s_adv_uuid[3],
                  s_adv_uuid[4], s_adv_uuid[5], s_adv_uuid[6], s_adv_uuid[7],
                  s_adv_uuid[8], s_adv_uuid[9], s_adv_uuid[10], s_adv_uuid[11],
                  s_adv_uuid[12], s_adv_uuid[13], s_adv_uuid[14], s_adv_uuid[15]);
        } else {
            ESP_LOGE(TAG, "ADV payload config failed: %d", p->adv_data_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        if (p->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_cfg_done |= SCAN_RSP_CFG_FLAG;
            ESP_LOGI(TAG, "SCAN_RSP payload configured.");
        } else {
            ESP_LOGE(TAG, "SCAN_RSP payload config failed: %d", p->scan_rsp_data_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        s_adv_running = (p->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
        ESP_LOGI(TAG, "Advertising %s.", s_adv_running ? "started" : "start FAILED");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_running = false;
        ESP_LOGI(TAG, "Advertising stopped.");
        if (s_lifeboat_enabled && !s_connected && s_adv_ready && s_stack_ready) {
            ESP_LOGW(TAG, "ADV stopped while idle — restarting.");
            ble_post(BLE_EVT_ADV_KICK);
        }
        break;

    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "Peer requested security -> REJECT (token-only auth).");
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, false);
        break;

    default:
        break;
    }

    /* When both payloads configured, mark ready and honor deferred start. */
    if ((s_adv_cfg_done & (ADV_CFG_FLAG | SCAN_RSP_CFG_FLAG)) ==
        (ADV_CFG_FLAG | SCAN_RSP_CFG_FLAG)) {
        if (!s_adv_ready) {
            s_adv_ready = true;
            if (s_adv_start_deferred) {
                s_adv_start_deferred = false;
                ble_post(BLE_EVT_ADV_KICK);
            }
        }
    }
}
