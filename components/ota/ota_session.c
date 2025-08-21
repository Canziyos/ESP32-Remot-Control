// ota_session: bounds/CRC/accounting & yields.

#include <string.h>
#include "ota_session.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "OTA-SESSION";

/* Tunables. */
#ifndef OTA_SESSION_YIELD_BYTES
#define OTA_SESSION_YIELD_BYTES  (64*1024)
#endif

/* --- CRC32 adapters (auto-select IDF helper if present). --- */
#if __has_include("esp_crc.h")
  #include "esp_crc.h"
  static inline uint32_t crc32_init(void){ return 0u; }
  static inline uint32_t crc32_update(uint32_t crc, const void *d, size_t n){ return esp_crc32_le(crc,(const uint8_t*)d,n); }
  static inline uint32_t crc32_final(uint32_t crc){ return crc; }
#elif __has_include("esp_rom_crc.h")
  #include "esp_rom_crc.h"
  static inline uint32_t crc32_init(void){ return 0u; }
  static inline uint32_t crc32_update(uint32_t crc, const void *d, size_t n){ return esp_rom_crc32_le(crc,(const uint8_t*)d,n); }
  static inline uint32_t crc32_final(uint32_t crc){ return crc; }
#else
  static inline uint32_t crc32_init(void){ return 0u; }
  static uint32_t crc32_update(uint32_t crc, const void *buf, size_t len){
      static const uint32_t poly = 0xEDB88320u;
      const uint8_t *p = (const uint8_t*)buf;
      while (len--){
          crc ^= *p++;
          for (int i = 0; i < 8; ++i){
              crc = (crc >> 1) ^ (poly & (-(int)(crc & 1)));
          }
      }
      return crc;
  }
  static inline uint32_t crc32_final(uint32_t crc){ return crc; }
#endif


void ota_session_crc_init(void) { /* no-op today. */ }

esp_err_t ota_session_begin(ota_session_t *s, size_t total_size, uint32_t crc32_expect, const char *source) {
    if (!s || total_size == 0) return ESP_ERR_INVALID_ARG;
    if (s->active) return ESP_ERR_INVALID_STATE;

    memset(s, 0, sizeof(*s));
    esp_err_t e = ota_writer_begin(total_size, &s->wr);
    if (e != ESP_OK) return e;

    s->active         = true;
    s->bytes_expected = total_size;
    s->crc_expect     = crc32_expect;
    s->crc_running    = crc32_init();
    s->yield_bytes    = OTA_SESSION_YIELD_BYTES;
    s->since_yield    = 0;
    snprintf(s->source, sizeof(s->source), "%.7s", (source && *source) ? source : "XPORT");

    ESP_LOGI(TAG, "begin: %s total=%u dst=%s@0x%06x.",
             s->source, (unsigned)total_size,
             s->wr.dst ? s->wr.dst->label : "?",
             s->wr.dst ? (unsigned)s->wr.dst->address : 0);

    return ESP_OK;
}

esp_err_t ota_session_write(ota_session_t *s, const void *data, size_t len) {
    if (!s || !s->active || !data || len == 0) return ESP_ERR_INVALID_ARG;
    if (s->bytes_written + len > s->bytes_expected) {
        ESP_LOGE(TAG, "write: overflow (%u + %u > %u).",
                 (unsigned)s->bytes_written, (unsigned)len, (unsigned)s->bytes_expected);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t e = ota_writer_write(&s->wr, data, len);
    if (e != ESP_OK) return e;

    s->bytes_written += len;
    s->crc_running    = crc32_update(s->crc_running, data, len);
    s->since_yield   += len;
    if (s->since_yield >= s->yield_bytes) {
        vTaskDelay(1);
        s->since_yield = 0;
    }
    return ESP_OK;
}

esp_err_t ota_session_finish(ota_session_t *s) {
    if (!s || !s->active) return ESP_ERR_INVALID_STATE;
    if (s->bytes_written != s->bytes_expected) {
        ESP_LOGE(TAG, "finish: short image (%u/%u).",
                 (unsigned)s->bytes_written, (unsigned)s->bytes_expected);
        ota_writer_abort(&s->wr);
        s->active = false;
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t calc = crc32_final(s->crc_running);
    if (s->crc_expect && calc != s->crc_expect) {
        ESP_LOGE(TAG, "finish: CRC mismatch calc=%08X expect=%08X.", calc, s->crc_expect);
        ota_writer_abort(&s->wr);
        s->active = false;
        return ESP_ERR_INVALID_CRC;
    }

    esp_err_t e = ota_writer_end(&s->wr);
    s->active = false;
    if (e != ESP_OK) return e;

    ESP_LOGI(TAG, "finish: %s OK (%u bytes).", s->source, (unsigned)s->bytes_written);
    return ESP_OK;
}

void ota_session_abort(ota_session_t *s, const char *reason_opt) {
    if (!s || !s->active) return;
    ESP_LOGW(TAG, "abort: %s.", reason_opt ? reason_opt : "unknown");
    ota_writer_abort(&s->wr);
    s->active = false;
}
