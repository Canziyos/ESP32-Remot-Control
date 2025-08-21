// gatt_server.c
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_gatts_api.h"

#include "alerts.h"
#include "errsrc.h"
#include "syscoord.h"
#include "ota_bridge.h"      // ctrl/data/disconnect hooks for OTA over GATT

#include "gatt_server.h"
#include "gatt_priv.h"         // internal helpers, handle table, flags, etc.

static const char *TAG = "GATT.srv";

/* If fallback provides this, it will override at link time. */
__attribute__((weak)) void ble_set_connected(bool on) { (void)on; }

/* Shared state (defined here, extern'd in gatt_priv.h) */
esp_gatt_if_t g_gatts_if        = ESP_GATT_IF_NONE;
uint16_t      g_conn_id         = 0xFFFF;
uint16_t      g_mtu_payload     = 20;

bool          tx_notify_enabled    = false;
bool          es_notify_enabled    = false;
bool          alert_notify_enabled = false;

uint16_t      gatt_handle_table[EFBE_IDX_NB];
uint8_t       cccd_tx_val[2]    = {0,0};
uint8_t       cccd_err_val[2]   = {0,0};
uint8_t       cccd_alert_val[2] = {0,0};

ble_cmd_t    *g_ble_cli = NULL;

static void on_read_errsrc(void) {
    const char *err = errsrc_get();
    if (!err) err = "NONE";
    esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                 (uint16_t)strlen(err),
                                 (const uint8_t*)err);
}

static void on_read_alert(void) {
    alert_record_t rec; alert_latest(&rec);
    char line[128];
    int n = snprintf(line, sizeof(line),
                     "ALERT seq=%u code=%u %s",
                     (unsigned)rec.seq, (unsigned)rec.code, rec.detail);
    if (n < 0) n = 0;
    size_t used = strnlen(line, sizeof(line));
    esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                 (uint16_t)used,
                                 (const uint8_t*)line);
}

/* ---- GATTS dispatcher ---- */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        g_gatts_if = gatts_if;
        gatt_build_attr_table(gatts_if);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        gatt_on_attr_table_created(param);
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "Service started");
        syscoord_on_ble_service_started();
        break;

    case ESP_GATTS_MTU_EVT: {
        uint16_t mtu = param->mtu.mtu;
        /* ATT payload = MTU - 3; clamp within sane bounds */
        g_mtu_payload = (mtu > 23) ? (mtu - 3) : 20;
        if (g_mtu_payload < 1)   g_mtu_payload = 1;
        if (g_mtu_payload > 180) g_mtu_payload = 180;
        ESP_LOGI(TAG, "MTU updated: mtu=%u payload=%u", (unsigned)mtu, (unsigned)g_mtu_payload);
        break;
    }

    case ESP_GATTS_READ_EVT:
        if (param->read.handle == gatt_handle_table[IDX_ERRSRC_VAL]) {
            on_read_errsrc();
        } else if (param->read.handle == gatt_handle_table[IDX_ALERT_VAL]) {
            on_read_alert();
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        g_conn_id = param->connect.conn_id;
        tx_notify_enabled = es_notify_enabled = alert_notify_enabled = false;
        syscoord_on_ble_state(true);
        ble_set_connected(true);
        if (g_ble_cli) {
            ble_cmd_on_connect(g_ble_cli, gatts_if, g_conn_id, gatt_handle_table[IDX_TX_VAL]);
        }
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ble_ota_on_disconnect();
        syscoord_on_ble_state(false);
        ble_set_connected(false);
        if (g_ble_cli) ble_cmd_on_disconnect(g_ble_cli);
        g_conn_id = 0xFFFF;
        tx_notify_enabled = es_notify_enabled = alert_notify_enabled = false;
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == gatt_handle_table[IDX_RX_VAL]) {
            if (g_ble_cli) ble_cmd_on_rx(g_ble_cli, param->write.value, param->write.len);

        } else if (param->write.handle == gatt_handle_table[IDX_WIFI_VAL]) {
            gatt_on_wifi_cred_write(param->write.value, param->write.len);

        } else if (param->write.handle == gatt_handle_table[IDX_TX_CCC]) {
            tx_notify_enabled = (gatt_ccc_decode(param->write.value, param->write.len) & 0x0001) != 0;

        } else if (param->write.handle == gatt_handle_table[IDX_ERRSRC_CCC]) {
            es_notify_enabled = (gatt_ccc_decode(param->write.value, param->write.len) & 0x0001) != 0;
            if (es_notify_enabled) {
                gatt_server_notify_errsrc(errsrc_get());
            }

        } else if (param->write.handle == gatt_handle_table[IDX_ALERT_CCC]) {
            alert_notify_enabled = (gatt_ccc_decode(param->write.value, param->write.len) & 0x0001) != 0;
            if (alert_notify_enabled) {
                alert_record_t snap; alert_latest(&snap);
                gatt_alert_notify(&snap);
            }

        } else if (param->write.handle == gatt_handle_table[IDX_OTA_CTRL_VAL]) {
            ble_ota_on_ctrl_write(param->write.value, param->write.len);

        } else if (param->write.handle == gatt_handle_table[IDX_OTA_DATA_VAL]) {
            ble_ota_on_data_write(param->write.value, param->write.len);
        }
        break;

    default:
        break;
    }
}

void gatt_server_init(void)
{
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0x42));

    if (!g_ble_cli) g_ble_cli = ble_cmd_create();

    /* Syscoord owns alert subscription via syscoord_alert_sink hook. */
    errsrc_subscribe(gatt_server_notify_errsrc);

    ESP_LOGI(TAG, "GATT server registered.");
}
