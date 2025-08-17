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
