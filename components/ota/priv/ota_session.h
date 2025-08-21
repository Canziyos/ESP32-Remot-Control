#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ota_writer.h"   // internal dependency

#ifndef OTA_SESSION_YIELD_BYTES
#define OTA_SESSION_YIELD_BYTES (64*1024)
#endif

typedef struct {
    bool         active;
    ota_writer_t wr;
    size_t       bytes_expected;
    size_t       bytes_written;
    uint32_t     crc_expect;
    uint32_t     crc_running;
    char         source[8];      // "BLE"/"TCP"
    size_t       yield_bytes;
    size_t       since_yield;
} ota_session_t;

void ota_session_crc_init(void);
esp_err_t ota_session_begin(ota_session_t *s, size_t total_size, uint32_t crc32_expect, const char *source);
esp_err_t ota_session_write(ota_session_t *s, const void *data, size_t len);
esp_err_t ota_session_finish(ota_session_t *s);
void ota_session_abort(ota_session_t *s, const char *reason_opt);
