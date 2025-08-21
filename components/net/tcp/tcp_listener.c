#include <errno.h>
#include <stdatomic.h>
#include <unistd.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "tcp_server.h"
#include "tcp_priv.h"
#include "syscoord.h"

#define PORT 8080
static const char *TAG = "TCP.srv";

/* Track total TCP clients to inform policy */
static _Atomic int s_client_count = 0;

void tcp_on_client_connected(void) {
    int new_cnt = atomic_fetch_add(&s_client_count, 1) + 1;
    syscoord_on_tcp_clients(new_cnt);
}

void tcp_on_client_disconnected(void) {
    int new_cnt = atomic_fetch_sub(&s_client_count, 1) - 1;
    if (new_cnt < 0) {
        atomic_store(&s_client_count, 0);
        new_cnt = 0;
    }
    syscoord_on_tcp_clients(new_cnt);
}

static void server_task(void *pv) {
    (void)pv;

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "socket(): %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int one = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    (void)setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)); // optional

    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        ESP_LOGE(TAG, "bind(): %d", errno);
        close(s);
        vTaskDelete(NULL);
        return;
    }
    if (listen(s, 4) < 0) {
        ESP_LOGE(TAG, "listen(): %d", errno);
        close(s);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening on %d.", PORT);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c >= 0) {
            tcp_client_spawn(c);
        } else {
            // Only log non-transient errors to avoid noise
            if (errno != EINTR && errno != EAGAIN) {
                ESP_LOGW(TAG, "accept(): %d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    // (not reached)
}

void launch_tcp_server(void) {
    xTaskCreate(server_task, "tcp_srv", 4096, NULL, 4, NULL);
}
