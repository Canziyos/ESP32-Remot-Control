// ble_ota_stub.c
#include "ble_ota_stub.h"
#include "esp_log.h"


__attribute__((weak))
void ble_ota_on_ctrl_write(const uint8_t *data, uint16_t len) { (void)data; (void)len; }

__attribute__((weak))
void ble_ota_on_data_write(const uint8_t *data, uint16_t len) { (void)data; (void)len; }

__attribute__((weak))
void ble_ota_on_disconnect(void) {}
