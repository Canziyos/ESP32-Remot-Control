// .c, no-op build
#include "ota_bridge.h"
#include "esp_log.h"


#if !CONFIG_FEATURE_BLE_OTA
__attribute__((weak)) void ble_ota_on_ctrl_write(const uint8_t *data, uint16_t len) { (void)data; (void)len; }
__attribute__((weak)) void ble_ota_on_data_write(const uint8_t *data, uint16_t len) { (void)data; (void)len; }
__attribute__((weak)) void ble_ota_on_disconnect(void) {}
#endif
