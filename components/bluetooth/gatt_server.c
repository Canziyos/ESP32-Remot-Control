#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdio.h>
#include "commands.h"
#include "command.h"
#include "gatt_server.h"
#include "ble_fallback.h"
#include "alerts.h" 
#include "syscoord.h"
#include "errsrc.h"
#include "ble_cmd.h"

static const char *TAG = "GATT";
static ble_cmd_t *s_cli = NULL; 

/* ERRSRC notify control */
static bool errsrc_notify_enabled = false;   // CCC state for ERRSRC.
static char s_last_errsrc_sent[64] = "";     // last notified value.

/* UUIDs */
// ALERT characteristic UUID: efbe0500-fbfb-fbfb-fb4b-494545434956
static const uint8_t ALERT_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x05,0xBE,0xEF
};

/* 128-bit UUIDs. */
const uint8_t SERVICE_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x00,0xBE,0xEF
};
static const uint8_t RX_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x01,0xBE,0xEF
};
static const uint8_t TX_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x02,0xBE,0xEF
};
static const uint8_t WIFI_CRED_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x03,0xBE,0xEF
};
static const uint8_t ERRSRC_UUID[16] = {
    0x56,0x49,0x43,0x45,0x45,0x49,0x4B,0xFB,0xFB,0xFB,0xFB,0xFB,0x00,0x04,0xBE,0xEF
};

/* Properties */
static uint8_t rx_props = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t tx_props = ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_READ;
static uint8_t wifi_props = ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t errsrc_props = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t alert_props = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static bool alert_notify_enabled = false;

/* 16-bit helper UUIDs */
static uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

/* Attribute table indices */
enum {
    IDX_SVC = 0,
    IDX_RX_CHAR,
    IDX_RX_VAL,
    IDX_TX_CHAR,
    IDX_TX_VAL,
    IDX_TX_CCC,
    IDX_WIFI_CHAR,
    IDX_WIFI_VAL,
    IDX_ERRSRC_CHAR,
    IDX_ERRSRC_VAL,
    IDX_ERRSRC_CCC,
    IDX_ALERT_CHAR,
    IDX_ALERT_VAL,
    IDX_ALERT_CCC,
    HRS_IDX_NB,
};

/* Handles & state */
static uint16_t gatt_handle_table[HRS_IDX_NB];
static esp_gatt_if_t g_gatts_if = ESP_GATT_IF_NONE;
static uint16_t g_conn_id = 0xFFFF;
static bool tx_notify_enabled = false;

/* Negotiated MTU payload size (ATT_MTU - 3). Default 20 for MTU 23. */
static uint16_t g_mtu_payload = 20;

/* each CCCD its own 2-byte storage. */
static uint8_t cccd_tx_val[2] = {0x00, 0x00};
static uint8_t cccd_err_val[2] = {0x00, 0x00};
static uint8_t cccd_alert_val[2] = {0x00, 0x00};
static char last_errsrc[64] = "NONE";

/* ------------ Helpers ------------ */

static void errsrc_notify_if_changed(const char *current_err)
{
    if (!current_err) current_err = "NONE";

    /* Keep attribute fresh so READ returns latest even if CCC is off. */
    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_ERRSRC_VAL]) {
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                     strlen(current_err), (const uint8_t*)current_err);
    }

    /* If the same as last sent, do nothing (avoid spam). */
    if (strncmp(current_err, s_last_errsrc_sent, sizeof(s_last_errsrc_sent)) == 0) {
        return;
    }

    /* Notify only if CCC enabled and we have a connection. */
    if (errsrc_notify_enabled && g_conn_id != 0xFFFF && g_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id,
                                    gatt_handle_table[IDX_ERRSRC_VAL],
                                    strlen(current_err), (uint8_t *)current_err, false);
    }

    /* Remember what we sent. */
    strncpy(s_last_errsrc_sent, current_err, sizeof(s_last_errsrc_sent) - 1);
    s_last_errsrc_sent[sizeof(s_last_errsrc_sent) - 1] = '\0';
}

/* Send a line to TX (update readable value + optional notifications). */
void gatt_server_send_status(const char *s) {
    if (!s) return;

    size_t slen = strlen(s);
    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_TX_VAL]) {
        /* Keep latest line in the readable value for polling clients. */
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_TX_VAL],
                                     slen, (const uint8_t*)s);
    }

    if (!tx_notify_enabled || g_conn_id == 0xFFFF || g_gatts_if == ESP_GATT_IF_NONE) return;

    const uint8_t *p = (const uint8_t *)s;
    size_t len = slen;

    uint16_t max_chunk = g_mtu_payload;
    if (max_chunk < 1)  max_chunk = 1;
    if (max_chunk > 180) max_chunk = 180;

    while (len) {
        uint16_t chunk = (len > max_chunk) ? max_chunk : (uint16_t)len;
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id, gatt_handle_table[IDX_TX_VAL],
                                    chunk, (uint8_t *)p, false /* no confirm */);
        p += chunk;
        len -= chunk;
    }
    const uint8_t nl = '\n';
    esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id, gatt_handle_table[IDX_TX_VAL],
                                1, (uint8_t *)&nl, false);
}

/* Push alerts into the ALERT characteristic */
void gatt_alert_notify(const alert_record_t *rec)
{
    if (!rec) return;

    char line[128];
    int n = snprintf(line, sizeof(line), "ALERT seq=%u code=%u %s",
                     (unsigned)rec->seq, (unsigned)rec->code, rec->detail);
    if (n < 0) n = 0;
    /* Make sure we don't claim more than the actual buffer contents (NUL excluded). */
    size_t used = strnlen(line, sizeof(line));

    /* Keep the ALERT characteristic value up to date for READs. */
    if (g_gatts_if != ESP_GATT_IF_NONE && gatt_handle_table[IDX_ALERT_VAL]) {
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                     (uint16_t)used, (const uint8_t*)line);
    }

    /* Notify subscribers if they enabled the CCC. */
    if (alert_notify_enabled && g_conn_id != 0xFFFF && g_gatts_if != ESP_GATT_IF_NONE) {
        esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id,
                                    gatt_handle_table[IDX_ALERT_VAL],
                                    (uint16_t)used, (uint8_t*)line, false);
    }
}

/* Notify ERRSRC changes via subscription */
void gatt_server_notify_errsrc(const char *err)
{
    if (!err || !*err) err = "NONE";
    errsrc_notify_if_changed(err);
}

/* Write Wi-Fi credentials characteristic -> route through command parser */
static void on_wifi_cred_write(const uint8_t *data, uint16_t len) {
    if (!data || !len) return;

    char buf[200];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, data, len);
    buf[len] = '\0';

    /* Form 1: "<ssid>\n<pwd>" OR Form 2: "SETWIFI <ssid> <pwd>" */
    char ssid[33] = {0};
    char pwd[65]  = {0};
    bool ok = false;

    char *nl = strchr(buf, '\n');
    if (nl) {
        *nl = '\0';
        strncpy(ssid, buf, sizeof(ssid) - 1);
        strncpy(pwd, nl + 1, sizeof(pwd) - 1);
        ok = (ssid[0] != '\0'); /* allow empty pwd for OPEN. */
    } else {
        char cmd[16] = {0};
        if (sscanf(buf, "%15s %32s %64s", cmd, ssid, pwd) == 3 &&
            strcasecmp(cmd, "SETWIFI") == 0) {
            ok = true;
        }
    }

    if (!ok) {
        ESP_LOGW(TAG, "Wi-Fi creds format invalid.");
        gatt_server_send_status("BADFMT");
        return;
    }

    /* Build a textual command and route via the normal command path. */
    char cmdline[120];
    int n = snprintf(cmdline, sizeof(cmdline), "setwifi %s %s", ssid, pwd);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(cmdline)) cmdline[sizeof(cmdline)-1] = '\0';

    if (s_cli) {
        size_t L = strnlen(cmdline, sizeof(cmdline));
        if (L < sizeof(cmdline) - 1) cmdline[L++] = '\n';   // line terminator.
        ble_cmd_on_rx(s_cli, (const uint8_t*)cmdline, (uint16_t)L);
    }
}

/* GATTS event handler */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        g_gatts_if = gatts_if;
        /* Build the whole attribute table in one go. */
        esp_ble_gatts_create_attr_tab((esp_gatts_attr_db_t[]){
            [IDX_SVC] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
                sizeof(SERVICE_UUID), sizeof(SERVICE_UUID), (uint8_t *)SERVICE_UUID} },

            [IDX_RX_CHAR] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
                sizeof(uint8_t), sizeof(uint8_t), &rx_props} },
            [IDX_RX_VAL] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_128, (uint8_t *)RX_UUID, ESP_GATT_PERM_WRITE, 512, 0, NULL} },

            [IDX_TX_CHAR] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
              sizeof(uint8_t), sizeof(uint8_t), &tx_props} },
            [IDX_TX_VAL] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_128, (uint8_t *)TX_UUID, ESP_GATT_PERM_READ,
              512, 0, NULL} },
            [IDX_TX_CCC] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
              sizeof(cccd_tx_val), sizeof(cccd_tx_val), cccd_tx_val} },

            [IDX_WIFI_CHAR] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
              sizeof(uint8_t), sizeof(uint8_t), &wifi_props} },
            [IDX_WIFI_VAL] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_128, (uint8_t *)WIFI_CRED_UUID, ESP_GATT_PERM_WRITE,
              128, 0, NULL} },

            [IDX_ERRSRC_CHAR] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
              sizeof(uint8_t), sizeof(uint8_t), &errsrc_props} },
            [IDX_ERRSRC_VAL] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_128, (uint8_t *)ERRSRC_UUID, ESP_GATT_PERM_READ,
              sizeof(last_errsrc), sizeof(last_errsrc), (uint8_t *)last_errsrc} },
            [IDX_ERRSRC_CCC] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
              sizeof(cccd_err_val), sizeof(cccd_err_val), cccd_err_val} },

            [IDX_ALERT_CHAR] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
              sizeof(uint8_t), sizeof(uint8_t), &alert_props} },
            [IDX_ALERT_VAL] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_128, (uint8_t *)ALERT_UUID, ESP_GATT_PERM_READ,
              128, 0, NULL} },
            [IDX_ALERT_CCC] = { {ESP_GATT_AUTO_RSP},
            {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
              ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
              sizeof(cccd_alert_val), sizeof(cccd_alert_val), cccd_alert_val} },
        }, gatts_if, HRS_IDX_NB, 0);
        break;

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK) {
            memcpy(gatt_handle_table, param->add_attr_tab.handles, sizeof(gatt_handle_table));

            /* Set clean initial values before starting service */
            const char *init  = "OK";
            const char *einit = "NONE";
            const char *ainit = "ALERT seq=0 code=0";

            esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_TX_VAL],
                                         strlen(init),  (const uint8_t*)init);
            esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                         strlen(einit), (const uint8_t*)einit);
            esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                         strlen(ainit), (const uint8_t*)ainit);

            esp_ble_gatts_start_service(gatt_handle_table[IDX_SVC]);
        } else {
            ESP_LOGE(TAG, "attr table create failed 0x%x.", param->add_attr_tab.status);
        }
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "Service started. Starting advertising.");
        ble_start_advertising();   /* Only if lifeboat is enabled */
        break;

    case ESP_GATTS_MTU_EVT:
        g_mtu_payload = (param->mtu.mtu > 23) ? (param->mtu.mtu - 3) : 20;
        if (g_mtu_payload < 1)   g_mtu_payload = 1;
        if (g_mtu_payload > 180) g_mtu_payload = 180;
        ESP_LOGI(TAG, "MTU updated. mtu=%d payload=%u.", param->mtu.mtu, g_mtu_payload);
        break;

    case ESP_GATTS_READ_EVT:
        if (param->read.handle == gatt_handle_table[IDX_ERRSRC_VAL]) {
            const char *err = errsrc_get();
            if (!err) err = "NONE";
            esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                         strlen(err), (const uint8_t*)err);
            ESP_LOGI(TAG, "ERRSRC read -> /'%s'.", err);

        } else if (param->read.handle == gatt_handle_table[IDX_ALERT_VAL]) {
            alert_record_t rec; alert_latest(&rec);

            char line[128];
            int n = snprintf(line, sizeof(line),
                             "ALERT seq=%u code=%u %s",
                             (unsigned)rec.seq, (unsigned)rec.code, rec.detail);
            if (n < 0) n = 0;
            size_t used = strnlen(line, sizeof(line));

            esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                         (uint16_t)used, (const uint8_t*)line);
            ESP_LOGI(TAG, "ALERT read -> '%s'.", line);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        g_conn_id = param->connect.conn_id;
        syscoord_on_ble_state(true);
        ble_set_connected(true);
        if (!s_cli) {
            s_cli = ble_cmd_create();
        }
        ble_cmd_on_connect(s_cli, g_gatts_if, g_conn_id,
                           gatt_handle_table[IDX_TX_VAL]);  // TX notify handle
     break;

    case ESP_GATTS_DISCONNECT_EVT:
        syscoord_on_ble_state(false);
        ble_set_connected(false);
        if (s_cli) {
            ble_cmd_on_disconnect(s_cli);
            // re-create per new connection?
            // ble_cmd_destroy(s_cli); s_cli = NULL;
        }
        g_conn_id = 0xFFFF; 
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == gatt_handle_table[IDX_RX_VAL]) {
            // Console RX over BLE -> bridge to command parser
            if (s_cli) {
                ble_cmd_on_rx(s_cli, param->write.value, param->write.len);
            } else {
                ESP_LOGW(TAG, "RX write before CLI init");
            }

        } else if (param->write.handle == gatt_handle_table[IDX_WIFI_VAL]) {
            // Special Wi-Fi creds characteristic
            on_wifi_cred_write(param->write.value, param->write.len);

        } else if (param->write.handle == gatt_handle_table[IDX_TX_CCC]) {
            // TX notify CCC
            if (param->write.len == 2) {
                uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                tx_notify_enabled = (cfg & 0x0001) != 0;
                ESP_LOGI(TAG, "TX notify %s.", tx_notify_enabled ? "ENABLED" : "DISABLED");
            }

        } else if (param->write.handle == gatt_handle_table[IDX_ERRSRC_CCC]) {
            // ERRSRC notify CCC
            if (param->write.len == 2) {
                uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                bool new_enabled = (cfg & 0x0001) != 0;
                errsrc_notify_enabled = new_enabled;
                ESP_LOGI(TAG, "ERRSRC notify %s.", new_enabled ? "ENABLED" : "DISABLED");
                if (new_enabled) {
                    s_last_errsrc_sent[0] = '\0'; // force first push
                    errsrc_notify_if_changed(errsrc_get());
                }
            }

        } else if (param->write.handle == gatt_handle_table[IDX_ALERT_CCC]) {
            // ALERT notify CCC
            if (param->write.len == 2) {
                uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                alert_notify_enabled = (cfg & 0x0001) != 0;
                ESP_LOGI(TAG, "ALERT notify %s.", alert_notify_enabled ? "ENABLED" : "DISABLED");
                if (alert_notify_enabled) {
                    alert_record_t snap; alert_latest(&snap);
                    gatt_alert_notify(&snap); // send one snapshot now
                }
            }
        }
        break;

    default:
        break;
    }
}

void gatt_server_init(void) {
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0x42));
    errsrc_subscribe(gatt_server_notify_errsrc);   /* auto-push on change */
    alerts_subscribe(gatt_alert_notify);
    ESP_LOGI(TAG, "GATT server registered.");
}
