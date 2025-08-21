#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *dst;
} ota_writer_t;

esp_err_t ota_writer_begin(size_t total_size, ota_writer_t *wr);
esp_err_t ota_writer_write(ota_writer_t *wr, const void *data, size_t len);
esp_err_t ota_writer_end(ota_writer_t *wr);
void ota_writer_abort(ota_writer_t *wr);
