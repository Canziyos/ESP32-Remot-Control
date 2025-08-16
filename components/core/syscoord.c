// components/core/syscoord.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include "esp_ota_ops.h"

#include "syscoord.h"
#include "esp_log.h"
#include "monitor.h"        // monitor_set_timeout_cb (used for explicit failure -> recovery)
#include "alerts.h"
#include "ble_fallback.h"
#include "bootflag.h"

static const char *TAG = "SYSCOORD";

/* ---- state ---- */
static _Atomic sc_mode_t g_mode = SC_MODE_STARTUP;

/* ---- internal events & worker ---- */
typedef enum { SYS_EVT_ENTER_RECOVERY } sys_evt_t;
static QueueHandle_t s_sys_q = NULL;
static TaskHandle_t  s_sys_task = NULL;

/* Heavy BLE work must run in a normal task, not in a timer callback. */
static void syscoord_worker(void *arg) {
    sys_evt_t ev;
    for (;;) {
        if (xQueueReceive(s_sys_q, &ev, portMAX_DELAY) == pdTRUE) {
            switch (ev) {
            case SYS_EVT_ENTER_RECOVERY:
                // Bring up BLE stack and start lifeboat advertising.
                ble_fallback_init();
                ble_lifeboat_set(true);
                alert_raise(ALERT_BLE_FATAL, "recovery mode: no control after rollback");
                break;
            }
        }
    }
}

/* ---- internals ---- */
static void set_mode(sc_mode_t m) {
    sc_mode_t prev = atomic_exchange(&g_mode, m);
    if (prev == m) return;

    ESP_LOGI(TAG, "mode: %d -> %d", prev, m);

    switch (m) {
    case SC_MODE_STARTUP:
        /* nothing special */
        break;

    case SC_MODE_WAIT_CONTROL:
        /* No timers, no delays.
           Hide BLE while we wait for Wi-Fi events (Got IP or explicit failure). */
        ble_lifeboat_set(false);
        ble_fallback_stop();
        break;

    case SC_MODE_NORMAL:
        /* Control path proven (Wi-Fi up / TCP ready). */
        ble_lifeboat_set(false);
        ble_fallback_stop();
        bootflag_set_post_rollback(false);
        break;

    case SC_MODE_RECOVERY:
        /* BLE bring-up happens in worker task. */
        break;
    }
}

/* Monitor reports an explicit failure: request RECOVERY via worker. */
static void on_second_failure(void) {
    const sys_evt_t ev = SYS_EVT_ENTER_RECOVERY;
    if (s_sys_q) (void)xQueueSend(s_sys_q, &ev, 0);
}

/* ---- public API ---- */
void syscoord_init(void) {
    ESP_LOGI(TAG, "System coordinator initialized.");

    /* Defensive hygiene: if running factory, clear any stale rollback flag. */
    {
        const esp_partition_t *run = esp_ota_get_running_partition();
        if (run && run->type == ESP_PARTITION_TYPE_APP &&
            run->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            bootflag_set_post_rollback(false);
        }
    }

    /* Start worker before we subscribe to monitor. */
    s_sys_q = xQueueCreate(4, sizeof(sys_evt_t));
    xTaskCreate(syscoord_worker, "syscoord.wkr", 4096, NULL, 5, &s_sys_task);

    /* Subscribe: monitor will call this only on real failures (no AP/auth/DHCP). */
    monitor_set_timeout_cb(on_second_failure);

    /* Wait for Wi-Fi events (Got IP ⇒ NORMAL, or monitor ⇒ RECOVERY). */
    set_mode(SC_MODE_WAIT_CONTROL);
}

void syscoord_control_path_ok(const char *source) {
    (void)source;
    set_mode(SC_MODE_NORMAL);
}

void syscoord_on_wifi_state(bool up) {
    ESP_LOGI(TAG, "Wi-Fi: %s", up ? "UP" : "DOWN");
    if (!up && atomic_load(&g_mode) == SC_MODE_NORMAL) {
        /* If Wi-Fi died after being healthy, go back to waiting. */
        set_mode(SC_MODE_WAIT_CONTROL);
    }
    /* No BLE here; only the worker starts BLE after explicit monitor failure. */
}

void syscoord_on_tcp_clients(int count) {
    (void)count;
}

void syscoord_on_ble_state(bool connected) {
    (void)connected;
}

void syscoord_on_no_control_path(void) {
    /* Manual escalation -> go through worker. */
    const sys_evt_t ev = SYS_EVT_ENTER_RECOVERY;
    if (s_sys_q) (void)xQueueSend(s_sys_q, &ev, 0);
}

sc_mode_t syscoord_get_mode(void) {
    return atomic_load(&g_mode);
}
