#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Perform OTA update from a connected client.
 *
 * The caller should have already parsed "OTA <size> <crc32>" and pass <size>.
 * This function streams exactly `image_size` bytes (firmware), then reads a
 * 4-byte trailing CRC32 (little-endian). If the running CRC matches the tail,
 * it writes the image to the next OTA slot, sets the boot partition, replies
 * "OK\n", and reboots. The new image will boot in PENDING_VERIFY state.
 */
esp_err_t ota_perform(int client_fd, uint32_t image_size);
// Transport-agnostic OTA API (usable from TCP or BLE)
esp_err_t ota_begin_xport(size_t total_size, uint32_t crc32_expect, const char *source);
esp_err_t ota_write_xport(const uint8_t *data, size_t len);
esp_err_t ota_finish_xport(void);
void      ota_abort_xport(const char *reason);

// Existing TCP helper (kept for compatibility).
esp_err_t ota_perform(int client_fd, uint32_t image_size);

