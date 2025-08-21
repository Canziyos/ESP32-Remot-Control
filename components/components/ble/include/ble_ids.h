// include/ble_ids.h
#pragma once
#include <stdint.h>

/* UUIDs are LSB=>MSB byte order as required by ESP-IDF. */

extern const uint8_t SERVICE_UUID[16];
extern const uint8_t RX_UUID[16];
extern const uint8_t TX_UUID[16];
extern const uint8_t WIFI_CRED_UUID[16];
extern const uint8_t ERRSRC_UUID[16];
extern const uint8_t ALERT_UUID[16];
extern const uint8_t UUID_EFBE_OTA_CTRL[16];
extern const uint8_t UUID_EFBE_OTA_DATA[16];
