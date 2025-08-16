#include "freertos/FreeRTOS.h"
#include "freertos/task.h"     // ADDED
#include "freertos/queue.h"    // ADDED
#include "freertos/timers.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stdbool.h>

#include "ble_fallback.h"
#include "gatt_server.h"

typedef enum {
    BLE_EVT_ADV_KICK,      // (re)start advertising if allowed
} ble_evt_t;

static QueueHandle_t s_ble_q   = NULL;
static TaskHandle_t  s_ble_wkr = NULL;
static const char *TAG = "BLE";
static uint8_t s_adv_uuid[16];

/* State */
static bool s_stack_ready        = false;        // controller+bluedroid up
static bool s_adv_ready          = false;        // adv payload configured
static bool s_adv_running        = false;        // advertising currently on
static bool s_connected          = false;        // link state
static bool s_lifeboat_enabled   = false;        // policy gate from syscoord
static bool s_adv_start_deferred = false;

/* Simple ADV watchdog to re-arm advertising if it ever stops while idle. */
static TimerHandle_t s_adv_watch = NULL;

/* Advertising params (PUBLIC address for stable MAC while debugging). */
static uint8_t s_adv_cfg_done = 0;
#define ADV_CFG_FLAG      (1 << 0)
#define SCAN_RSP_CFG_FLAG (1 << 1)
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,                 // ~20 ms
    .adv_int_max       = 0x40,                 // ~40 ms
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* --- fwd decls --- */
static void start_adv_now(void);

static void ble_worker(void *arg) {
    ble_evt_t ev;
    for (;;) {
        if (xQueueReceive(s_ble_q, &ev, portMAX_DELAY) == pdTRUE) {
            switch (ev) {
            case BLE_EVT_ADV_KICK:
                // Do all heavy GAP here, never in timer callback.
                start_adv_now();
                break;
            default:
                break;
            }
        }
    }
}

// Small helper to enqueue from any context that is NOT an ISR.
// (FreeRTOS software timers run in a normal task, not an ISR.)
static inline void ble_post(ble_evt_t ev) {
    if (s_ble_q) (void)xQueueSend(s_ble_q, &ev, 0);
}

/* Basic stop (tolerant). */
void ble_stop_advertising(void) {
    if (!s_adv_running) return;                // avoid noisy INVALID_STATE logs
    esp_err_t e = esp_ble_gap_stop_advertising();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "stop advertising: %s", esp_err_to_name(e));
    }
}

/* Lifeboat visibility – ONLY syscoord should call this. */
void ble_lifeboat_set(bool enable) {
    s_lifeboat_enabled = enable;
    ESP_LOGI(TAG, "lifeboat: %s", enable ? "ENABLED" : "DISABLED");

    if (!enable) {
        s_adv_start_deferred = false;          // clear any pending start
        ble_stop_advertising();
        return;
    }

    // Enable: request start via worker if we can, otherwise defer.
    if (!s_connected && s_adv_ready && s_stack_ready) {
        ble_post(BLE_EVT_ADV_KICK);
    } else {
        s_adv_start_deferred = true;
    }
}

void ble_set_provisioning(bool on) {
    // Kept for API compatibility; currently no link-layer pairing/encryption.
    ESP_LOGI(TAG, "Provisioning flag: %s (token-only auth)", on ? "ENABLED" : "DISABLED");
}

/* Internal: start advertising now (preconditions already met). */
static void start_adv_now(void) {
    if (!s_lifeboat_enabled) {
        ESP_LOGI(TAG, "lifeboat disabled -> not advertising");
        return;
    }
    if (!s_stack_ready || !s_adv_ready) {
        s_adv_start_deferred = true;
        return;
    }
    if (s_connected) return; // no advertising while connected

    esp_err_t e = esp_ble_gap_start_advertising(&s_adv_params);
    if (e == ESP_ERR_INVALID_STATE) {          // if already running/busy, bounce once
        (void)esp_ble_gap_stop_advertising();
        e = esp_ble_gap_start_advertising(&s_adv_params);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(e));
    }
}

/* Watchdog: if not connected and not currently advertising, request a kick
 * (only if lifeboat on). Never call GAP here; just post to the worker. */
static void adv_watch_cb(TimerHandle_t t) {
    (void)t;
    if (s_lifeboat_enabled && !s_connected && s_adv_ready && s_stack_ready && !s_adv_running) {
        ESP_LOGW(TAG, "ADV watchdog: requesting restart.");
        ble_post(BLE_EVT_ADV_KICK);
    }
}

/* Public: called from gatt_server.c (ESP_GATTS_START_EVT). */
void ble_start_advertising(void) {
    // Respect policy: only advertise if lifeboat is enabled.
    if (!s_lifeboat_enabled) {
        ESP_LOGI(TAG, "ble_start_advertising ignored (lifeboat disabled).");
        return;
    }
    if (!s_adv_ready || !s_stack_ready) {
        s_adv_start_deferred = true;
        ESP_LOGI(TAG, "ADV requested but stack/payload not ready — deferring.");
        return;
    }
    ble_post(BLE_EVT_ADV_KICK);               // ensure GAP call happens in worker
}

/* Let GATT tell us about link state (call on CONNECT/DISCONNECT). */
void ble_set_connected(bool on) {
    s_connected = on;
    if (!on) {
        // Return to lifeboat advertising if policy allows.
        if (s_lifeboat_enabled) {
            if (s_adv_ready && s_stack_ready) ble_post(BLE_EVT_ADV_KICK);
            else s_adv_start_deferred = true;
        }
    } else {
        ESP_LOGI(TAG, "Marked as CONNECTED; advertising remains off until disconnect.");
    }
}

/* GAP callback */
static void gap_evt(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p) {
    switch (ev) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (p->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_cfg_done |= ADV_CFG_FLAG;
            ESP_LOGI(TAG, "ADV payload configured.");
            ESP_LOGI(TAG, "ADV UUID128 (LSB->MSB): "
                     "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                     SERVICE_UUID[0], SERVICE_UUID[1], SERVICE_UUID[2], SERVICE_UUID[3],
                     SERVICE_UUID[4], SERVICE_UUID[5], SERVICE_UUID[6], SERVICE_UUID[7],
                     SERVICE_UUID[8], SERVICE_UUID[9], SERVICE_UUID[10], SERVICE_UUID[11],
                     SERVICE_UUID[12], SERVICE_UUID[13], SERVICE_UUID[14], SERVICE_UUID[15]);
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
        // if we aren't connected, keep it running if policy allows.
        if (s_lifeboat_enabled && !s_connected && s_adv_ready && s_stack_ready) {
            ESP_LOGW(TAG, "ADV stopped while idle — restarting.");
            ble_post(BLE_EVT_ADV_KICK);
        }
        break;

    /* If a central asks for security, reject (app-level token auth only). */
    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "Peer requested security -> REJECT (token-only auth).");
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, false);
        break;

    default:
        break;
    }

    /* When both adv and scan_rsp payloads are configured, mark ready and honor defers. */
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

void ble_fallback_init(void) {
    if (s_stack_ready) {
        ESP_LOGW(TAG, "BLE stack already active — skipping init.");
        return;
    }

    // --- Create worker first (4 KB stack is fine) ---
    if (!s_ble_q)   s_ble_q = xQueueCreate(8, sizeof(ble_evt_t));
    if (!s_ble_wkr) xTaskCreate(ble_worker, "ble.wkr", 4096, NULL, 5, &s_ble_wkr);

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

    /* GAP callback + device name. */
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_evt));
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
        .p_service_uuid   = s_adv_uuid,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

    s_adv_ready  = false;
    s_adv_start_deferred = false;
    s_connected = false;
    s_adv_running = false;
    s_adv_cfg_done = 0;

    memcpy(s_adv_uuid, SERVICE_UUID, sizeof(s_adv_uuid));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan_rsp_data));

    /* Bring up the GATT service; it will call ble_start_advertising() once started. */
    gatt_server_init();

    if (!s_adv_watch) {
        s_adv_watch = xTimerCreate("adv_watch", pdMS_TO_TICKS(2000), pdTRUE, NULL, adv_watch_cb);
        if (s_adv_watch) xTimerStart(s_adv_watch, 0);
    }

    ESP_LOGI(TAG, "BLE fallback init done. Waiting for GATT start and ADV payload ready.");
}

/* stop BLE (keeps state for next init). */
void ble_fallback_stop(void) {
    ble_stop_advertising();
    if (s_adv_watch) { xTimerStop(s_adv_watch, 0); /* keep timer allocated */ }

    /* Clear transient state so future init behaves predictably. */
    s_connected = false;
    s_adv_running = false;
    s_adv_start_deferred = false;
    // Note: we keep s_stack_ready/s_adv_ready true if the stack stays enabled.
    // If more RAM needed, we can fully deinit the stack here.
    // if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) esp_bluedroid_disable();
    // if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) esp_bluedroid_deinit();
    // if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
    // if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
    // s_stack_ready = false; s_adv_ready = false;
}
