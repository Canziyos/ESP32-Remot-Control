#pragma once
#include <stdint.h>
#include "esp_gatts_api.h"

// Opaque type
typedef struct ble_cmd ble_cmd_t;

ble_cmd_t* ble_cmd_create(void);
void       ble_cmd_destroy(ble_cmd_t* cli);

void ble_cmd_on_connect(ble_cmd_t* cli,
                        esp_gatt_if_t gatts_if,
                        uint16_t conn_id,
                        uint16_t tx_char_handle);

void ble_cmd_on_rx(ble_cmd_t* cli,
                   const uint8_t* data,
                   uint16_t len);

void ble_cmd_on_disconnect(ble_cmd_t* cli);
