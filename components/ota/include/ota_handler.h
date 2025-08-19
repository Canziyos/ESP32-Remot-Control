#pragma once

#include <stdint.h>
#include <stddef.h>   // for size_t
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Perform OTA update over an already-authenticated TCP connection.
 *
 * Protocol:
 *   Host sends: "OTA <size> <crc32>\n"
 *   Device replies: "ACK\n"
 *   Host sends: <size> bytes of image, then a 4-byte little-endian CRC32 trailer.
 *
 * The running CRC32 is the zlib/LE variant (seed=0, no final XOR) and must match
 * the 4-byte trailer. On success:
 *   - image is written to next OTA slot,
 *   - boot partition is switched,
 *   - "OK\n" is sent,
 *   - device reboots. The new image boots in PENDING_VERIFY state.
 */
esp_err_t ota_perform(int client_fd, uint32_t image_size);

/* Transport-agnostic OTA API (usable from TCP or BLE). */
esp_err_t ota_begin_xport(size_t total_size, uint32_t crc32_expect, const char *source);
esp_err_t ota_write_xport(const uint8_t *data, size_t len);
esp_err_t ota_finish_xport(void);
void      ota_abort_xport(const char *reason);

#ifdef __cplusplus
}  // extern "C"
#endif
