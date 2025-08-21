// ota_handler: thin adapter that uses writer/session
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
#include "lwip/sockets.h"

#include "app_cfg.h"
#include "ota_session.h"

static const char *TAG     = "OTA";
static const char *TAG_TCP = "OTA-TCP";

/* Tunables. */
#define RECV_TIMEOUT_S   OTA_RECV_TIMEOUT_S
#define WRITE_BUF_SZ     OTA_WRITE_BUF_SZ

/* --- helpers for TCP path. --- */
static ssize_t recv_fully(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t r = recv(fd, p + got, len - got, 0);
        if (r == 0) return 0;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
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

/* Public: TCP OTA perform using session. Same behavior as before. */
esp_err_t ota_perform(int client_fd, uint32_t image_size) {
    if (image_size == 0) {
        send_line(client_fd, "ERR bad_size");
        return ESP_ERR_INVALID_SIZE;
    }

    struct timeval tv = { .tv_sec = RECV_TIMEOUT_S, .tv_usec = 0 };
    (void)setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    send_line(client_fd, "ACK");

    ota_session_t s;
    ota_session_crc_init();
    esp_err_t e = ota_session_begin(&s, image_size, /*crc32_expect=*/0, "TCP"); // CRC checked via trailing tail.
    if (e != ESP_OK) {
        send_line(client_fd, "ERR ota_begin");
        return e;
    }

    uint8_t *buf = (uint8_t *)malloc(WRITE_BUF_SZ);
    if (!buf) {
        send_line(client_fd, "ERR nomem");
        ota_session_abort(&s, "malloc failed");
        return ESP_ERR_NO_MEM;
    }

    uint32_t received = 0;
    while (received < image_size) {
        size_t want = image_size - received;
        if (want > WRITE_BUF_SZ) want = WRITE_BUF_SZ;

        ssize_t r = recv_fully(client_fd, buf, want);
        if (r <= 0) {
            send_line(client_fd, "ERR recv_payload");
            free(buf);
            ota_session_abort(&s, "recv_payload");
            return ESP_FAIL;
        }

        e = ota_session_write(&s, buf, (size_t)r);
        if (e != ESP_OK) {
            send_line(client_fd, "ERR ota_write");
            free(buf);
            ota_session_abort(&s, "ota_write");
            return e;
        }

        received += (uint32_t)r;
    }

    /* Trailing CRC32 (little-endian) for TCP. */
    uint8_t tail[4];
    if (recv_fully(client_fd, tail, sizeof(tail)) != (ssize_t)sizeof(tail)) {
        send_line(client_fd, "ERR recv_crc");
        free(buf);
        ota_session_abort(&s, "recv_crc");
        return ESP_FAIL;
    }
    uint32_t tail_crc = (uint32_t)tail[0] |
                        ((uint32_t)tail[1] << 8) |
                        ((uint32_t)tail[2] << 16) |
                        ((uint32_t)tail[3] << 24);

    /* Check CRC via session’s running CRC. */
    // The session tracked CRC over payload only. Finish with an explicit expect.
    s.crc_expect = tail_crc;

    e = ota_session_finish(&s);
    free(buf);
    if (e != ESP_OK) {
        if (e == ESP_ERR_INVALID_CRC) send_line(client_fd, "CRCFAIL");
        else                          send_line(client_fd, "ERR ota_end");
        return e;
    }

    send_line(client_fd, "OK");
    ESP_LOGI(TAG_TCP, "OTA OK, rebooting.");

    close_quiet(client_fd);
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
    return ESP_OK; /* not reached. */
}

/* BLE xport compatibility API kept the same, just delegating to session. */
static ota_session_t s_ble;  /* Single BLE session. */

esp_err_t ota_begin_xport(size_t total_size, uint32_t crc32_expect, const char *source) {
    ota_session_crc_init();
    return ota_session_begin(&s_ble, total_size, crc32_expect, source ? source : "BLE");
}

esp_err_t ota_write_xport(const uint8_t *data, size_t len) {
    return ota_session_write(&s_ble, data, len);
}

esp_err_t ota_finish_xport(void) {
    return ota_session_finish(&s_ble);
}

void ota_abort_xport(const char *reason) {
    ota_session_abort(&s_ble, reason);
}
