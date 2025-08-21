// priv/ota_bridge
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Called by GATT attribute writes; implemented by OTA module or stub. */
void ble_ota_on_ctrl_write(const uint8_t *data, uint16_t len);
void ble_ota_on_data_write(const uint8_t *data, uint16_t len);
void ble_ota_on_disconnect(void);

#ifdef __cplusplus
}
#endif
