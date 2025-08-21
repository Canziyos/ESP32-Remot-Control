// components/ble/fallback/fb_core.c.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stdbool.h>

#include "ble_fallback.h"   // public API
#include "gatt_server.h"    // gatt_server_init()
#include "ble_ids.h"        // SERVICE_UUID
#include "fb_priv.h"        // worker, GAP bridge, watchdog, shared state

static const char *TAG = "BLE.fb.core";

/* State (owned here, extern'd by fb_priv.h) */
bool s_stack_ready = false;
bool s_adv_ready = false;
bool s_adv_running = false;
bool s_connected = false;
bool s_lifeboat_enabled = false;
bool s_adv_start_deferred = false;

TimerHandle_t s_adv_watch = NULL;

uint8_t  s_adv_uuid[16];
uint8_t  s_adv_cfg_done = 0;

esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,                 // ~20 ms
    .adv_int_max = 0x40,                 // ~40 ms
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* Local helper: stop advertising via GAP bridge */
static void ble_stop_advertising(void) {
    fb_adv_stop_public();
}

/* Public API */

void ble_lifeboat_set(bool enable) {
    s_lifeboat_enabled = enable;
    ESP_LOGI(TAG, "lifeboat: %s", enable ? "ENABLED" : "DISABLED");

    if (!enable) {
        s_adv_start_deferred = false;
        ble_stop_advertising();
        return;
    }
    if (!s_connected && s_adv_ready && s_stack_ready) {
        ble_post(BLE_EVT_ADV_KICK);
    } else {
        s_adv_start_deferred = true;
    }
}

void ble_set_provisioning(bool on) {
    ESP_LOGI(TAG, "Provisioning flag: %s (token-only auth)", on ? "ENABLED" : "DISABLED");
}

void ble_start_advertising(void) {
    if (!s_lifeboat_enabled) {
        ESP_LOGI(TAG, "ble_start_advertising ignored (lifeboat disabled).");
        return;
    }
    if (!s_adv_ready || !s_stack_ready) {
        s_adv_start_deferred = true;
        ESP_LOGI(TAG, "ADV requested but stack/payload not ready — deferring.");
        return;
    }
    ble_post(BLE_EVT_ADV_KICK);
}

/* Exported (non-static) so it can override the weak symbol in gatt_server.c */
void ble_set_connected(bool on) {
    s_connected = on;
    if (!on) {
        if (s_lifeboat_enabled) {
            if (s_adv_ready && s_stack_ready) ble_post(BLE_EVT_ADV_KICK);
            else s_adv_start_deferred = true;
        }
    } else {
        ESP_LOGI(TAG, "Marked CONNECTED; advertising off until disconnect.");
    }
}

void ble_fallback_init(void) {
    if (s_stack_ready) {
        ESP_LOGW(TAG, "BLE stack already active — skipping init.");
        return;
    }

    /* Worker first */
    fb_worker_init_once();

    /* Free classic BT RAM. */
    esp_err_t r = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(r);

    /* Controller + Bluedroid */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t cr = esp_bt_controller_init(&bt_cfg);
    if (cr != ESP_OK && cr != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(cr);
    esp_err_t en = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (en != ESP_OK && en != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(en);

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        ESP_ERROR_CHECK(esp_bluedroid_init());
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
        ESP_ERROR_CHECK(esp_bluedroid_enable());
    }
    s_stack_ready = true;

    /* GAP callback + device name */
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(fb_gap_evt));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name("LoPy4"));

    /* ADV payloads: NAME in scan response; UUID in primary ADV. */
    static esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = false,
        .include_txpower = false,
        .min_interval = 0x20,
        .max_interval = 0x40,
        .appearance = 0x00,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = 16,
        .p_service_uuid = s_adv_uuid,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    static esp_ble_adv_data_t scan_rsp_data = {
        .set_scan_rsp = true,
        .include_name = true,
        .include_txpower = true,
        .appearance = 0,
        .service_uuid_len = 16,
        .p_service_uuid = s_adv_uuid,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    s_adv_ready = false;
    s_adv_start_deferred = false;
    s_connected = false;
    s_adv_running = false;
    s_adv_cfg_done = 0;

    memcpy(s_adv_uuid, SERVICE_UUID, sizeof(s_adv_uuid));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan_rsp_data));

    /* Bring up GATT; it will call syscoord_on_ble_service_started() and then we kick ADV. */
    gatt_server_init();

    fb_watchdog_start_if_needed();

    ESP_LOGI(TAG, "BLE fallback init done. Waiting for GATT start and ADV payload ready.");
}

void ble_fallback_stop(void) {
    ble_stop_advertising();
    fb_watchdog_stop();

    /* Clear transient state (keep stack enabled unless you need RAM back) */
    s_connected          = false;
    s_adv_running        = false;
    s_adv_start_deferred = false;

    /* If you need to fully free BT/BLE RAM, uncomment below:
       s_stack_ready = false; s_adv_ready = false;
       if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED)      esp_bluedroid_disable();
       if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) esp_bluedroid_deinit();
       if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
       if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)  esp_bt_controller_deinit();
    */
}
