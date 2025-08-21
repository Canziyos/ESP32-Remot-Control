#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ota_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------- Writer (flash/partition) ------------------- */

typedef struct {
    esp_ota_handle_t handle;   // Valid after begin until end/abort.
    const esp_partition_t *dst;      // Chosen target partition.
} ota_writer_t;

// Low-level flashing operations.
esp_err_t ota_writer_begin(size_t total_size, ota_writer_t *wr);
esp_err_t ota_writer_write(ota_writer_t *wr, const void *data, size_t len);
esp_err_t ota_writer_end(ota_writer_t *wr);
void ota_writer_abort(ota_writer_t *wr);

/* ------------------- Session (bounds + CRC + yield) -------------- */

// Default yield threshold (bytes written between vTaskDelay(1) calls).
#ifndef OTA_SESSION_YIELD_BYTES
#define OTA_SESSION_YIELD_BYTES (64 * 1024)
#endif

typedef struct {
    bool active;
    ota_writer_t wr;
    size_t bytes_expected;
    size_t bytes_written;
    uint32_t crc_expect;     // 0 = skip final CRC check.
    uint32_t crc_running;
    char source[8];      // "BLE"/"TCP" for logs.
    size_t yield_bytes;
    size_t since_yield;
} ota_session_t;

// Session lifecycle.
void ota_session_crc_init(void); // no-op today, keeps options open.
esp_err_t ota_session_begin(ota_session_t *s, size_t total_size, uint32_t crc32_expect, const char *source);
esp_err_t ota_session_write(ota_session_t *s, const void *data, size_t len);
esp_err_t ota_session_finish(ota_session_t *s);
void ota_session_abort(ota_session_t *s, const char *reason_opt);

#ifdef __cplusplus
}
#endif
