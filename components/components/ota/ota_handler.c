#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"     // use lwIP’s socket API on ESP-IDF

// CRC32 helpers: prefer IDF-provided, fallback to local.
#if __has_include("esp_crc.h")
  #include "esp_crc.h"
  #define CRC32_UPDATE esp_crc32_le
#elif __has_include("esp_rom_crc.h")
  #include "esp_rom_crc.h"
  #define CRC32_UPDATE esp_rom_crc32_le
#else
  static uint32_t CRC32_UPDATE(uint32_t crc, const uint8_t *buf, size_t len) {
      crc = ~crc;
      for (size_t i = 0; i < len; ++i) {
          uint32_t c = (crc ^ buf[i]) & 0xFF;
          for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (-(int)(c & 1)));
          crc = (crc >> 8) ^ c;
      }
      return ~crc;
  }
#endif

static const char *TAG = "OTA";

// Tunables.
#define RECV_TIMEOUT_S   30        // generous to tolerate flash erase stalls
#define WRITE_BUF_SZ     4096
#define YIELD_BYTES      (64*1024) // feed WDT every 64 KiB

static ssize_t recv_fully(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, p + got, len - got, 0);
        if (r == 0) return 0;      // peer closed
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static void send_line(int fd, const char *s) {
    (void)send(fd, s, strlen(s), 0);
    (void)send(fd, "\n", 1, 0);
}

esp_err_t ota_perform(int client_fd, uint32_t image_size)
{
    // Set a forgiving receive timeout (per recv() call).
    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Tell the sender we are ready before heavy work like erase.
    send_line(client_fd, "ACK");

    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (!dst) {
        ESP_LOGE(TAG, "No OTA partition.");
        send_line(client_fd, "ERR no_ota_partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(dst, image_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        send_line(client_fd, "ERR ota_begin");
        return err;
    }

    uint8_t *buf = (uint8_t *)malloc(WRITE_BUF_SZ);
    if (!buf) {
        ESP_LOGE(TAG, "malloc failed");
        send_line(client_fd, "ERR nomem");
        esp_ota_abort(handle);
        return ESP_ERR_NO_MEM;
    }

    uint32_t running_crc = 0;
    uint32_t received = 0;
    uint32_t since_yield = 0;

    while (received < image_size) {
        size_t want = image_size - received;
        if (want > WRITE_BUF_SZ) want = WRITE_BUF_SZ;

        ssize_t r = recv_fully(client_fd, buf, want);
        if (r <= 0) {
            ESP_LOGE(TAG, "recv payload failed, r=%d errno=%d", (int)r, errno);
            send_line(client_fd, "ERR recv_payload");
            free(buf);
            esp_ota_abort(handle);
            return ESP_FAIL;
        }

        running_crc = CRC32_UPDATE(running_crc, buf, (size_t)r);
        err = esp_ota_write(handle, buf, (size_t)r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            send_line(client_fd, "ERR ota_write");
            free(buf);
            esp_ota_abort(handle);
            return err;
        }

        received += (uint32_t)r;
        since_yield += (uint32_t)r;
        if (since_yield >= YIELD_BYTES) {
            vTaskDelay(1);
            since_yield = 0;
        }
    }

    // Read trailing CRC32 (little-endian).
    uint8_t tail[4];
    if (recv_fully(client_fd, tail, sizeof(tail)) != (ssize_t)sizeof(tail)) {
        ESP_LOGE(TAG, "recv trailing CRC failed");
        send_line(client_fd, "ERR recv_crc");
        free(buf);
        esp_ota_abort(handle);
        return ESP_FAIL;
    }
    uint32_t trailing_crc = ((uint32_t)tail[0]) |
                            ((uint32_t)tail[1] << 8) |
                            ((uint32_t)tail[2] << 16) |
                            ((uint32_t)tail[3] << 24);

    if (running_crc != trailing_crc) {
        ESP_LOGE(TAG, "CRC mismatch: calc=0x%08X, tail=0x%08X", running_crc, trailing_crc);
        send_line(client_fd, "CRCFAIL");
        free(buf);
        esp_ota_abort(handle);
        return ESP_FAIL;
    }

    free(buf);

    // Finalize and switch boot partition.
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        send_line(client_fd, "ERR ota_end");
        return err;
    }
    err = esp_ota_set_boot_partition(dst);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        send_line(client_fd, "ERR set_boot");
        return err;
    }

    // Do NOT mark valid here. After reboot, image will be PENDING_VERIFY.
    // wifi.c will mark valid on GOT_IP, which prevents rollback and keeps BLE hidden.
    send_line(client_fd, "OK");
    ESP_LOGI(TAG, "OTA OK, rebooting.");

    // Let the client see OK before reset.
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    vTaskDelay(pdMS_TO_TICKS(750));

    esp_restart();
    return ESP_OK; // not reached
}
