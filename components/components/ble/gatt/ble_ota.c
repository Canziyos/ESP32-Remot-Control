// ble_ota.c, BLE OTA adapter (uses ota_writer)
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "ota_bridge.h"    // declares the hooks we override
#include "ota_handler.h"      // ota_begin_xport / write_xport / finish_xport / abort_xport
#include "monitor.h"          // health_monitor_control_ok(...)
#include "syscoord.h"         // optional gating via mode
#include "gatt_server.h"      // gatt_server_send_status(...)

static const char *TAG = "BLE-OTA";

/* Convenience wrapper: push a line to the BLE TX characteristic. */
static inline void ble_tx_send(const char *s) {
    gatt_server_send_status(s ? s : "");
}

/* ---- BLE OTA state ---- */
typedef struct {
    bool      active;
    uint32_t  total;
    uint32_t  written;
    uint32_t  expect_crc;
    uint32_t  expect_seq;
    uint32_t  next_prog_mark;
} ble_ota_state_t;

static ble_ota_state_t s_bo;

static inline void ble_ota_reset(void) { memset(&s_bo, 0, sizeof(s_bo)); }

/* little-endian helpers */
static inline uint32_t rd_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }

/* ---------- Hooks called by gatt_server.c ---------- */

void ble_ota_on_ctrl_write(const uint8_t *data, uint16_t len)
{
    if (!data || !len) return;

    char line[128];
    size_t n = (len < sizeof(line)-1) ? len : sizeof(line)-1;
    memcpy(line, data, n);
    line[n] = '\0';
    for (char *p = line; *p; ++p) if (*p == '\r' || *p == '\n') *p = ' ';

    ESP_LOGI(TAG, "CTRL: %s", line);

    if (strncmp(line, "BL_OTA START", 12) == 0) {
        if (s_bo.active) { ble_tx_send("ERR BUSY"); return; }

        uint32_t size = 0, crc = 0;
        if (sscanf(line + 12, "%" SCNu32 " %" SCNx32, &size, &crc) != 2 || size == 0) {
            ble_tx_send("ERR BADFMT");
            return;
        }

        // Gate by mode: allow BLE OTA only in RECOVERY
        if (syscoord_get_mode() != SC_MODE_RECOVERY) {
            ble_tx_send("ERR FORBIDDEN");
            return;
        }
        esp_err_t err = ota_begin_xport(size, crc, "BLE");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_begin_xport failed: %s", esp_err_to_name(err));
            ble_tx_send("ERR BEGIN");
            return;
        }

        s_bo.active         = true;
        s_bo.total          = size;
        s_bo.written        = 0;
        s_bo.expect_crc     = crc;     // checked inside ota_finish_xport
        s_bo.expect_seq     = 0;
        s_bo.next_prog_mark = 256 * 1024;

        // Latch health monitor so the device doesn’t try to rollback mid-flash.
        health_monitor_control_ok("BLE-OTA");

        ble_tx_send("ACK START");
        return;
    }

    if (strncmp(line, "BL_OTA FINISH", 13) == 0) {
        if (!s_bo.active) { ble_tx_send("ERR NOACTIVE"); return; }

        esp_err_t err = ota_finish_xport();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_finish_xport failed: %s", esp_err_to_name(err));
            ble_tx_send("ERR FINISH");
            ble_ota_reset();
            return;
        }

        ble_tx_send("OK REBOOTING");
        ESP_LOGI(TAG, "BLE-OTA complete: %u bytes.", (unsigned)s_bo.written);
        ble_ota_reset();                       // avoid finalize-on-disconnect race.
        vTaskDelay(pdMS_TO_TICKS(400));
        esp_restart(); // no return
    }

    if (strncmp(line, "BL_OTA ABORT", 12) == 0) {
        if (!s_bo.active) { ble_tx_send("ERR NOACTIVE"); return; }
        ota_abort_xport("ble abort");
        ble_ota_reset();
        ble_tx_send("OK ABORTED");
        return;
    }

    ble_tx_send("ERR UNKNOWN");
}

void ble_ota_on_data_write(const uint8_t *data, uint16_t len)
{
    if (!s_bo.active || !data || len < 6) return;

    uint32_t seq  = rd_le32(data);
    uint16_t blen = rd_le16(data + 4);
    if ((uint32_t)len != (uint32_t)6 + blen) {
        ESP_LOGW(TAG, "DATA bad frame: len=%u hdr.len=%u", (unsigned)len, (unsigned)blen);
        return;
    }

    if (seq != s_bo.expect_seq) {
        ESP_LOGW(TAG, "DATA out-of-order: got=%u expect=%u", (unsigned)seq, (unsigned)s_bo.expect_seq);
        if (seq < s_bo.expect_seq) return; // drop duplicates
        return; // drop ahead-of-time frames
    }

    if (s_bo.written + blen > s_bo.total) {
        ESP_LOGE(TAG, "DATA overflow: %u + %u > %u", (unsigned)s_bo.written, (unsigned)blen, (unsigned)s_bo.total);
        return;
    }

    const uint8_t *payload = data + 6;
    esp_err_t err = ota_write_xport(payload, blen);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_write_xport failed: %s", esp_err_to_name(err));
        ble_tx_send("ERR WRITE");
        ota_abort_xport("write_fail");
        ble_ota_reset();
        return;
    }

    s_bo.expect_seq++;
    s_bo.written += blen;

    if (s_bo.written >= s_bo.next_prog_mark || s_bo.written == s_bo.total) {
        char msg[48];
        snprintf(msg, sizeof(msg), "PROG %u/%u",
                 (unsigned)s_bo.written, (unsigned)s_bo.total);
        ble_tx_send(msg);
        s_bo.next_prog_mark += 256 * 1024;
    }
    /* Finalize as soon as the last chunk arrives: FINISH becomes optional. */
    if (s_bo.written == s_bo.total) {
        ESP_LOGI(TAG, "Image complete on DATA stream; finalizing (no FINISH required)...");
        esp_err_t err = ota_finish_xport();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_finish_xport failed at end-of-data: %s", esp_err_to_name(err));
            ble_tx_send("ERR FINISH");
            ota_abort_xport("finish_fail");
            ble_ota_reset();
            return;
        }
        ble_tx_send("OK REBOOTING");
        ESP_LOGI(TAG, "BLE-OTA complete: %u bytes.", (unsigned)s_bo.written);
        ble_ota_reset();                       /* avoid double-finalize on disconnect */
        vTaskDelay(pdMS_TO_TICKS(400));
        esp_restart(); // no return
    }
}

void ble_ota_on_disconnect(void)
{
    if (!s_bo.active) return;

    if (s_bo.written >= s_bo.total && s_bo.total > 0) {
        ESP_LOGW(TAG, "BLE dropped but image is complete (%u/%u). Finalizing...",
                 (unsigned)s_bo.written, (unsigned)s_bo.total);
        if (ota_finish_xport() == ESP_OK) {
            ESP_LOGI(TAG, "BLE-OTA complete on disconnect; rebooting.");
            vTaskDelay(pdMS_TO_TICKS(400));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "finish_xport failed after complete image; aborting.");
            ota_abort_xport("finish_fail");
        }
    } else {
        ESP_LOGW(TAG, "BLE link dropped during OTA — aborting.");
        ota_abort_xport("ble disconnect");
    }
    ble_ota_reset();
}
