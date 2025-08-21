#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TCP entry (used by TCP server) */
esp_err_t ota_perform(int client_fd, uint32_t image_size);

/* Transport-agnostic xport API (used by BLE) */
esp_err_t ota_begin_xport(size_t total_size, uint32_t crc32_expect, const char *source);
esp_err_t ota_write_xport(const uint8_t *data, size_t len);
esp_err_t ota_finish_xport(void);
void ota_abort_xport(const char *reason);

#ifdef __cplusplus
}
#endif
