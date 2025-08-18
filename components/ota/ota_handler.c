#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "lwip/sockets.h"

#include "esp_check.h"

/* -------------------------------------------------------------------------
 * CRC32 helpers
 *
 * For the TCP path we compare against a trailing CRC32 that your tool
 * computes with Python's zlib.crc32(data) & 0xFFFFFFFF — i.e., seed=0,
 * no special final xor on the call site. IDF's esp_crc32_le() behaves
 * as an incremental update function with a seed, so we keep seed=0 and
 * do not invert at the end.
 * ------------------------------------------------------------------------- */
#if __has_include("esp_crc.h")
  #include "esp_crc.h"
  #define CRC32_INIT()                 (0u)
  #define CRC32_UPDATE(crc,d,n)        esp_crc32_le((crc), (const uint8_t*)(d), (n))
  #define CRC32_FINAL(crc)             (crc)
#elif __has_include("esp_rom_crc.h")
  #include "esp_rom_crc.h"
  #define CRC32_INIT()                 (0u)
  #define CRC32_UPDATE(crc,d,n)        esp_rom_crc32_le((crc), (const uint8_t*)(d), (n))
  #define CRC32_FINAL(crc)             (crc)
#else
  static inline uint32_t CRC32_INIT(void) { return 0u; }
  static uint32_t CRC32_UPDATE(uint32_t crc, const void *buf, size_t len) {
      static const uint32_t poly = 0xEDB88320u;
      const uint8_t *p = (const uint8_t*)buf;
      while (len--) {
          crc ^= *p++;
          for (int i = 0; i < 8; ++i) {
              crc = (crc >> 1) ^ (poly & (-(int)(crc & 1)));
          }
      }
      return crc;
  }
  #define CRC32_FINAL(crc)             (crc)
#endif

/* ------------------------------------------------------------------------- */

static const char *TAG     = "OTA";
static const char *TAG_X   = "OTA-X";

/* Tunables */
#define RECV_TIMEOUT_S   30         /* tolerate flash erase stalls */
#define WRITE_BUF_SZ     4096
#define YIELD_BYTES      (64*1024)  /* feed WDT roughly every 64 KiB */

/* =========================================================================
 *  BLE "xport" helpers (used by your GATT OTA)
 * ========================================================================= */

typedef struct {
    bool                      active;
    esp_ota_handle_t          handle;
    const esp_partition_t    *dst;
    size_t                    bytes_expected;
    size_t                    bytes_written;
    uint32_t                  crc_expect;   /* expected full-image CRC (seed=0) */
    uint32_t                  crc_running;  /* running CRC */
    char                      source[8];    /* "BLE" / "TCP" for logs */
} ota_xport_ctx_t;

static ota_xport_ctx_t s_ox;  /* zero-inited */

static inline void ota_xport_clear(void) {
    memset(&s_ox, 0, sizeof(s_ox));
}

esp_err_t ota_begin_xport(size_t total_size, uint32_t crc32_expect, const char *source)
{
    if (s_ox.active) {
        ESP_LOGW(TAG_X, "begin: OTA already active");
        return ESP_ERR_INVALID_STATE;
    }
    if (total_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (!dst) {
        ESP_LOGE(TAG_X, "begin: no update partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (total_size > dst->size) {
        ESP_LOGE(TAG_X, "begin: image too large (%u > %u)",
                 (unsigned)total_size, (unsigned)dst->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(dst, total_size, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_X, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(&s_ox, 0, sizeof(s_ox));
    s_ox.active         = true;
    s_ox.handle         = h;
    s_ox.dst            = dst;
    s_ox.bytes_expected = total_size;
    s_ox.crc_expect     = crc32_expect;
    s_ox.crc_running    = CRC32_INIT();
    snprintf(s_ox.source, sizeof(s_ox.source), "%.7s", (source && *source) ? source : "XPORT");

    ESP_LOGI(TAG_X, "begin: %s total=%" PRIu32 " dst=%s@0x%06x",
             s_ox.source, (uint32_t)total_size, dst->label, (unsigned)dst->address);
    return ESP_OK;
}

esp_err_t ota_write_xport(const uint8_t *data, size_t len)
{
    if (!s_ox.active)                 return ESP_ERR_INVALID_STATE;
    if (!data || len == 0)            return ESP_ERR_INVALID_ARG;
    if (s_ox.bytes_written + len > s_ox.bytes_expected) {
        ESP_LOGE(TAG_X, "write: overflow (%u + %u > %u)",
                 (unsigned)s_ox.bytes_written, (unsigned)len, (unsigned)s_ox.bytes_expected);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_write(s_ox.handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_X, "esp_ota_write failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ox.bytes_written += len;
    s_ox.crc_running = CRC32_UPDATE(s_ox.crc_running, data, len);

    /* Light yield every so often to keep WDT happy in long writes. */
    static size_t since_yield = 0;
    since_yield += len;
    if (since_yield >= YIELD_BYTES) {
        vTaskDelay(1);
        since_yield = 0;
    }

    return ESP_OK;
}

esp_err_t ota_finish_xport(void)
{
    if (!s_ox.active) return ESP_ERR_INVALID_STATE;

    if (s_ox.bytes_written != s_ox.bytes_expected) {
        ESP_LOGE(TAG_X, "finish: short image (%u/%u)",
                 (unsigned)s_ox.bytes_written, (unsigned)s_ox.bytes_expected);
        (void)esp_ota_abort(s_ox.handle);
        ota_xport_clear();
        return ESP_ERR_INVALID_SIZE;
    }

    const uint32_t calc = CRC32_FINAL(s_ox.crc_running);
    if (s_ox.crc_expect && calc != s_ox.crc_expect) {
        ESP_LOGE(TAG_X, "finish: CRC mismatch calc=%08X expect=%08X", calc, s_ox.crc_expect);
        (void)esp_ota_abort(s_ox.handle);
        ota_xport_clear();
        return ESP_ERR_INVALID_CRC;
    }

    esp_err_t err = esp_ota_end(s_ox.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_X, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_xport_clear();
        return err;
    }

    err = esp_ota_set_boot_partition(s_ox.dst);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_X, "set_boot failed: %s", esp_err_to_name(err));
        ota_xport_clear();
        return err;
    }

    ESP_LOGI(TAG_X, "finish: %s OK (%u bytes). Ready to reboot.",
             s_ox.source, (unsigned)s_ox.bytes_written);
    ota_xport_clear();
    return ESP_OK;
}

void ota_abort_xport(const char *reason)
{
    if (!s_ox.active) return;
    ESP_LOGW(TAG_X, "abort: %s", reason ? reason : "unknown");
    (void)esp_ota_abort(s_ox.handle);
    ota_xport_clear();
}

/* =========================================================================
 *  TCP OTA (simple streaming with trailing CRC32)
 * ========================================================================= */

static ssize_t recv_fully(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, p + got, len - got, 0);
        if (r == 0) return 0;               /* peer closed */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static inline void send_line(int fd, const char *s) {
    (void)send(fd, s, strlen(s), 0);
    (void)send(fd, "\n", 1, 0);
}

static inline void close_quiet(int fd) {
    if (fd < 0) return;
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

esp_err_t ota_perform(int client_fd, uint32_t image_size)
{
    if (image_size == 0) {
        send_line(client_fd, "ERR bad_size");
        return ESP_ERR_INVALID_SIZE;
    }

    /* Generous per-recv timeout to tolerate flash erase latency. */
    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Tell the sender we're ready *before* erase/write work. */
    send_line(client_fd, "ACK");

    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);
    if (!dst) {
        ESP_LOGE(TAG, "No OTA partition.");
        send_line(client_fd, "ERR no_ota_partition");
        return ESP_ERR_NOT_FOUND;
    }
    if (image_size > dst->size) {
        ESP_LOGE(TAG, "Image too large for %s (%u > %u)",
                 dst->label, (unsigned)image_size, (unsigned)dst->size);
        send_line(client_fd, "ERR image_too_large");
        return ESP_ERR_INVALID_SIZE;
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

    uint32_t running_crc = CRC32_INIT();
    uint32_t received    = 0;
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

        received    += (uint32_t)r;
        since_yield += (uint32_t)r;
        if (since_yield >= YIELD_BYTES) {
            vTaskDelay(1);
            since_yield = 0;
        }
    }

    /* Read trailing CRC32 (little-endian) and compare. */
    uint8_t tail[4];
    if (recv_fully(client_fd, tail, sizeof(tail)) != (ssize_t)sizeof(tail)) {
        ESP_LOGE(TAG, "recv trailing CRC failed");
        send_line(client_fd, "ERR recv_crc");
        free(buf);
        esp_ota_abort(handle);
        return ESP_FAIL;
    }
    uint32_t tail_crc = ((uint32_t)tail[0]) |
                        ((uint32_t)tail[1] << 8) |
                        ((uint32_t)tail[2] << 16) |
                        ((uint32_t)tail[3] << 24);

    running_crc = CRC32_FINAL(running_crc);
    if (running_crc != tail_crc) {
        ESP_LOGE(TAG, "CRC mismatch: calc=0x%08X, tail=0x%08X", running_crc, tail_crc);
        send_line(client_fd, "CRCFAIL");
        free(buf);
        esp_ota_abort(handle);
        return ESP_ERR_INVALID_CRC;
    }

    free(buf);

    /* Finalize and switch boot partition. */
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

    /* Do NOT mark valid here. After reboot the image is PENDING_VERIFY.
       Your Wi-Fi path (GOT_IP + TCP AUTH) will call
       esp_ota_mark_app_valid_cancel_rollback() via syscoord_control_path_ok(). */
    send_line(client_fd, "OK");
    ESP_LOGI(TAG, "OTA OK, rebooting.");

    /* Let the client see OK before reset. */
    close_quiet(client_fd);
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();

    return ESP_OK; /* not reached */
}
