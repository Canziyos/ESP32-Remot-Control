#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdatomic.h>   // _Atomic
#include <stdint.h>      // intptr_t
#include <unistd.h>      // close()
#include <errno.h>       // errno

#include "tcp_server.h"
#include "command.h"
#include "commands.h"
#include "syscoord.h"    // control-path + client-count hooks

#define PORT 8080
static const char *TAG = "TCP";

/* Track total TCP clients to inform policy (optional but useful). */
static _Atomic int s_client_count = 0;

/* Write helper for TCP */
static int tcp_write(const void *buf, int len, void *user) {
    int fd = (intptr_t)user;
    ESP_LOGD(TAG, "tcp_write(fd=%d, len=%d)", fd, len);
    return send(fd, buf, len, 0);
}

/* Log without leaking secrets (AUTH token, Wi-Fi pwd). */
static void log_sanitized_line(const char *line) {
    if (!line) { ESP_LOGI(TAG, "(null)"); return; }
    if (!strncasecmp(line, "AUTH ", 5)) {
        ESP_LOGI(TAG, "Received from TCP: 'AUTH ****'");
        return;
    }
    if (!strncasecmp(line, "SETWIFI ", 8)) {
        ESP_LOGI(TAG, "Received from TCP: 'SETWIFI **** ****'");
        return;
    }
    ESP_LOGI(TAG, "Received from TCP: '%s'", line);
}

/* Per-client task */
static void client_task(void *arg) {
    int fd = (intptr_t)arg;
    ESP_LOGI(TAG, "Client connected: fd=%d", fd);

    /* Bump client count and notify syscoord. */
    int new_cnt = ++s_client_count;
    syscoord_on_tcp_clients(new_cnt);

    char line[128];

    cmd_ctx_t ctx = {
        .authed   = false,
        .is_ble   = false,
        .tcp_fd   = fd,
        .ble_link = NULL,
        .write    = tcp_write
    };

    for (;;) {
        int n = recv(fd, line, sizeof(line) - 1, 0);
        if (n <= 0) break;
        line[n] = '\0';

        log_sanitized_line(line);

        /* Dispatch the line; handlers flip ctx.authed on successful AUTH. */
        cmd_dispatch_line(line, n, &ctx);
        /* No need to call syscoord here: commands.c already promotes to NORMAL
           via syscoord_mark_tcp_authed() + syscoord_control_path_ok("TCP"). */
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);

    /* Drop client count and notify syscoord. */
    new_cnt = --s_client_count;
    if (new_cnt < 0) new_cnt = 0;  // safety
    syscoord_on_tcp_clients(new_cnt);

    vTaskDelete(NULL);
}

/* Server listener */
static void server_task(void *pv) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(s, (void *)&a, sizeof a);
    listen(s, 4);
    ESP_LOGI(TAG, "Listening on %d.", PORT);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c >= 0) {
            xTaskCreate(client_task, "cli", 4096, (void *)(intptr_t)c, 5, NULL);
        }
    }
}

void launch_tcp_server(void) {
    xTaskCreate(server_task, "tcp_srv", 4096, NULL, 4, NULL);
}
