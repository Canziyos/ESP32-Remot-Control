// gatt_attrs.c
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_gatts_api.h"

#include "ble_ids.h"    // SERVICE_UUID, RX_UUID, TX_UUID ...
#include "gatt_priv.h"

static const char *TAG = "GATT.attrs";

/* Properties (constant) */
static const uint8_t rx_props = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t tx_props = ESP_GATT_CHAR_PROP_BIT_NOTIFY  | ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t wifi_props = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t errsrc_props = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t alert_props = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t ota_ctrl_props = ESP_GATT_CHAR_PROP_BIT_WRITE;      // write with response
static const uint8_t ota_data_props = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

/* 16-bit helper UUIDs */
static uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;


uint16_t gatt_ccc_decode(const uint8_t *val, uint16_t len)
{
    if (!val || !len) return 0;
    uint16_t v = val[0];
    if (len > 1) v |= ((uint16_t)val[1]) << 8;   // CCC is little-endian
    return v;
}

/* Storage is defined in gatt_server.c (externs are already in gatt_priv.h) */

void gatt_build_attr_table(esp_gatt_if_t gatts_if)
{
    g_gatts_if = gatts_if;

    /* Build the whole attribute table in one go */
    static esp_gatts_attr_db_t db[EFBE_IDX_NB];

    db[IDX_SVC] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
         sizeof(SERVICE_UUID), sizeof(SERVICE_UUID), (uint8_t *)SERVICE_UUID}
    };

    db[IDX_RX_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&rx_props}
    };
    db[IDX_RX_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)RX_UUID, ESP_GATT_PERM_WRITE, 512, 0, NULL}
    };

    db[IDX_TX_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&tx_props}
    };
    db[IDX_TX_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)TX_UUID, ESP_GATT_PERM_READ, 512, 0, NULL}
    };
    db[IDX_TX_CCC] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(cccd_tx_val), sizeof(cccd_tx_val), cccd_tx_val}
    };

    db[IDX_WIFI_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&wifi_props}
    };
    db[IDX_WIFI_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)WIFI_CRED_UUID, ESP_GATT_PERM_WRITE, 128, 0, NULL}
    };

    db[IDX_ERRSRC_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&errsrc_props}
    };
    db[IDX_ERRSRC_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)ERRSRC_UUID, ESP_GATT_PERM_READ, 64, 0, NULL}
    };
    db[IDX_ERRSRC_CCC] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(cccd_err_val), sizeof(cccd_err_val), cccd_err_val}
    };

    db[IDX_ALERT_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&alert_props}
    };
    db[IDX_ALERT_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)ALERT_UUID, ESP_GATT_PERM_READ, 128, 0, NULL}
    };
    db[IDX_ALERT_CCC] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid,
         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
         sizeof(cccd_alert_val), sizeof(cccd_alert_val), cccd_alert_val}
    };

    db[IDX_OTA_CTRL_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&ota_ctrl_props}
    };
    db[IDX_OTA_CTRL_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)UUID_EFBE_OTA_CTRL, ESP_GATT_PERM_WRITE, 512, 0, NULL}
    };

    db[IDX_OTA_DATA_CHAR] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
         sizeof(uint8_t), sizeof(uint8_t), (uint8_t *)&ota_data_props}
    };
    db[IDX_OTA_DATA_VAL] = (esp_gatts_attr_db_t){
        {ESP_GATT_AUTO_RSP},
        {ESP_UUID_LEN_128, (uint8_t *)UUID_EFBE_OTA_DATA, ESP_GATT_PERM_WRITE, 512, 0, NULL}
    };

    esp_err_t e = esp_ble_gatts_create_attr_tab(db, gatts_if, EFBE_IDX_NB, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "create_attr_tab: %s", esp_err_to_name(e));
    }
}

void gatt_on_attr_table_created(esp_ble_gatts_cb_param_t *param)
{
    if (param->add_attr_tab.status == ESP_GATT_OK &&
        param->add_attr_tab.num_handle == EFBE_IDX_NB)
    {
        memcpy(gatt_handle_table, param->add_attr_tab.handles, sizeof(gatt_handle_table));

        const char *init = "OK";
        const char *einit = "NONE";
        const char *ainit = "ALERT seq=0 code=0";

        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_TX_VAL],
                                     (uint16_t)strlen(init),  (const uint8_t*)init);
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ERRSRC_VAL],
                                     (uint16_t)strlen(einit), (const uint8_t*)einit);
        esp_ble_gatts_set_attr_value(gatt_handle_table[IDX_ALERT_VAL],
                                     (uint16_t)strlen(ainit), (const uint8_t*)ainit);

        esp_ble_gatts_start_service(gatt_handle_table[IDX_SVC]);
    } else {
        ESP_LOGE(TAG, "attr table create failed st=0x%x num=%d.",
                 param->add_attr_tab.status, param->add_attr_tab.num_handle);
    }
}
