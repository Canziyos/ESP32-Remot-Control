// gatt_priv.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Indexes into gatt_handle_table[] (from gatt_attrs.c) ----- */
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

  IDX_OTA_CTRL_CHAR,
  IDX_OTA_CTRL_VAL,

  IDX_OTA_DATA_CHAR,
  IDX_OTA_DATA_VAL,

  IDX_DHT_CHAR,
  IDX_DHT_VAL,
  IDX_DHT_CCC,

  EFBE_IDX_NB
};

/* ----- Opaque command parser context (ble_cmd.c) ----- */
typedef struct ble_cmd ble_cmd_t;

ble_cmd_t* ble_cmd_create(void);
void ble_cmd_destroy(ble_cmd_t* cli);
void ble_cmd_on_connect(ble_cmd_t* cli, esp_gatt_if_t gatts_if, uint16_t conn_id, uint16_t tx_char_handle);
void ble_cmd_on_rx(ble_cmd_t* cli, const uint8_t* data, uint16_t len);
void ble_cmd_on_disconnect(ble_cmd_t* cli);

/* ----- Attribute table + helpers (gatt_attrs.c / gatt_wifi_cred.c / gatt_notify.c) ----- */
void gatt_build_attr_table(esp_gatt_if_t gatts_if);
void gatt_on_attr_table_created(esp_ble_gatts_cb_param_t *param);
uint16_t gatt_ccc_decode(const uint8_t *val, uint16_t len);
void gatt_on_wifi_cred_write(const uint8_t *data, uint16_t len);
void gatt_server_notify_errsrc(const char *str);

/* Internal notifier used by syscoord hook override in gatt_notify.c
 * Keep it loose-typed so we don't pull alerts.h into public surface. */
void gatt_alert_notify(const void *rec_any); /* rec_any = const alert_record_t* */

/* ----- Shared state (defined in gatt_server.c) ----- */
extern esp_gatt_if_t g_gatts_if;
extern uint16_t g_conn_id;
extern uint16_t g_mtu_payload;

extern bool tx_notify_enabled;
extern bool es_notify_enabled;
extern bool alert_notify_enabled;

extern uint16_t  gatt_handle_table[EFBE_IDX_NB];
extern uint8_t cccd_tx_val[2];
extern uint8_t cccd_err_val[2];
extern uint8_t cccd_alert_val[2];

// for DHT
extern uint8_t cccd_dht_val[2];
extern bool dht_notify_enabled;


extern ble_cmd_t *g_ble_cli;

#ifdef __cplusplus
}
#endif
