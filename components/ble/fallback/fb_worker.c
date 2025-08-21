// components/ble/fallback/fb_worker.c
#include "fb_priv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BLE.fb.worker";

#define BLE_WORKER_STACK 4096
#define BLE_WORKER_PRIO 5
#define BLE_QUEUE_DEPTH 8

QueueHandle_t s_ble_q = NULL;
TaskHandle_t  s_ble_wkr = NULL;

static void ble_worker(void *arg) {
    (void)arg;
    ble_evt_t ev;
    for (;;) {
        if (xQueueReceive(s_ble_q, &ev, portMAX_DELAY) != pdTRUE) continue;

        switch (ev) {
        case BLE_EVT_ADV_KICK:
            fb_adv_kick();
            break;
        default:
            // future events can be handled here
            break;
        }
    }
}

void ble_post(ble_evt_t ev) {
    if (!s_ble_q) return;
    (void)xQueueSend(s_ble_q, &ev, 0);
}

/* Create worker/queue if needed */
void fb_worker_init_once(void) {
    if (!s_ble_q) {
        s_ble_q = xQueueCreate(BLE_QUEUE_DEPTH, sizeof(ble_evt_t));
        if (!s_ble_q) {
            ESP_LOGE(TAG, "Failed to create BLE event queue");
            return;
        }
    }
    if (!s_ble_wkr) {
        BaseType_t ok = xTaskCreate(ble_worker, "ble.wkr", BLE_WORKER_STACK, NULL, BLE_WORKER_PRIO, &s_ble_wkr);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create BLE worker task");
            s_ble_wkr = NULL;
        }
    }
}
