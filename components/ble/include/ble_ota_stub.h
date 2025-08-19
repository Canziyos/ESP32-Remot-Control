#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_ota_on_ctrl_write(const uint8_t *data, uint16_t len);
void ble_ota_on_data_write(const uint8_t *data, uint16_t len);
/* Optional but recommended: abort/reset on link drop */
void ble_ota_on_disconnect(void);

#ifdef __cplusplus
}
#endif
