// ota_writer: transport-agnostic: begin/write/finish/abort


#include "ota_writer.h"
#include "esp_log.h"

static const char *TAG = "OTA-WRITER";

esp_err_t ota_writer_begin(size_t total_size, ota_writer_t *wr) {
    if (!wr) return ESP_ERR_INVALID_ARG;
    wr->handle = 0;
    wr->dst    = esp_ota_get_next_update_partition(NULL);
    if (!wr->dst) {
        ESP_LOGE(TAG, "No OTA partition.");
        return ESP_ERR_NOT_FOUND;
    }
    if (total_size > wr->dst->size) {
        ESP_LOGE(TAG, "Image too large (%u > %u).", (unsigned)total_size, (unsigned)wr->dst->size);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t e = esp_ota_begin(wr->dst, total_size, &wr->handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s.", esp_err_to_name(e));
        return e;
    }
    return ESP_OK;
}

esp_err_t ota_writer_write(ota_writer_t *wr, const void *data, size_t len) {
    if (!wr || !wr->handle || !data || !len) return ESP_ERR_INVALID_ARG;
    return esp_ota_write(wr->handle, data, len);
}

esp_err_t ota_writer_end(ota_writer_t *wr) {
    if (!wr || !wr->handle || !wr->dst) return ESP_ERR_INVALID_STATE;
    esp_err_t e = esp_ota_end(wr->handle);
    if (e != ESP_OK) return e;
    e = esp_ota_set_boot_partition(wr->dst);
    return e;
}

void ota_writer_abort(ota_writer_t *wr) {
    if (!wr) return;
    if (wr->handle) (void)esp_ota_abort(wr->handle);
    wr->handle = 0;
    wr->dst    = NULL;
}
