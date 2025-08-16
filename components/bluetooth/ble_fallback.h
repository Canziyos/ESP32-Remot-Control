#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_gap_ble_api.h"

/* 128-bit service UUID from gatt_server.c (big-endian). */
extern const uint8_t SERVICE_UUID[16];

void ble_fallback_init(void);
void ble_fallback_stop(void);

/* BLE internals used by GATT server: */
void ble_start_advertising(void);   // used on disconnect
void ble_set_provisioning(bool on); // allow/deny new pairing
void ble_set_connected(bool on);

/* Lifeboat visibility – ONLY syscoord should call this. */
void ble_lifeboat_set(bool on);
