#include <string.h>       // memchr
#include <stdint.h>       // intptr_t
#include <limits.h>       // INT_MAX
#include <unistd.h>       // close, shutdown
#include <sys/socket.h>   // recv, send

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "command.h"
#include "commands.h"
#include "tcp_priv.h"

static const char *TAG = "TCP.cli";

/* Write helper for TCP (matches cmd_write_fn: size_t len) */
static int tcp_write(const void *buf, size_t len, void *user) {
    int fd = (int)(intptr_t)user;
    if (!buf || fd < 0) return 0;
    if (len > (size_t)INT_MAX) len = (size_t)INT_MAX;  // clamp for send()
    return send(fd, buf, (int)len, 0);
}

/* Log without leaking secrets (AUTH token, Wi-Fi pwd). */
static void log_sanitized_line(const char *line) {
    if (!line) { ESP_LOGI(TAG, "(null)"); return; }
    if (!strncasecmp(line, "AUTH ", 5))     { ESP_LOGI(TAG, "Received: 'AUTH ****'"); return; }
    if (!strncasecmp(line, "SETWIFI ", 8))  { ESP_LOGI(TAG, "Received: 'SETWIFI **** ****'"); return; }
    ESP_LOGI(TAG, "Received: '%s'", line);
}

static void client_task(void *arg) {
    int fd = (int)(intptr_t)arg;
    ESP_LOGI(TAG, "Client connected: fd=%d", fd);

    tcp_on_client_connected();

    cmd_ctx_t ctx = {
        .authed = false,
        .xport  = CMD_XPORT_TCP,
        .u.tcp_fd = fd,
        .write  = tcp_write,
    };

    char    line[128];
    size_t  linelen = 0;
    uint8_t buf[256];

    for (;;) {
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        const uint8_t *p = buf;
        size_t left = (size_t)n;

        while (left) {
            const uint8_t *nl = memchr(p, '\n', left);
            size_t chunk = nl ? (size_t)(nl - p) : left;

            // Copy chunk into the line buffer, skipping '\r'.
            for (size_t j = 0; j < chunk; ++j) {
                char c = (char)p[j];
                if (c == '\r') continue;
                if (linelen < sizeof(line) - 1) line[linelen++] = c;
            }

            if (nl) {
                line[linelen] = '\0';
                if (linelen) {
                    log_sanitized_line(line);
                    cmd_dispatch_line(line, linelen, &ctx);
                }
                linelen = 0;
                p    = nl + 1;
                left = left - chunk - 1;
            } else {
                p    += chunk;
                left -= chunk;
            }
        }
    }

    if (linelen) {
        line[linelen] = '\0';
        log_sanitized_line(line);
        cmd_dispatch_line(line, linelen, &ctx);
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);

    tcp_on_client_disconnected();
    vTaskDelete(NULL);
}

void tcp_client_spawn(int fd) {
    xTaskCreate(client_task, "tcp_cli", 4096, (void *)(intptr_t)fd, 5, NULL);
}
