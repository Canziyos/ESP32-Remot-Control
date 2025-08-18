// components/core/syscoord.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <string.h>          // strcmp
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "syscoord.h"
#include "monitor.h"         // monitor_set_timeout_cb, health_monitor_start
#include "alerts.h"
#include "ble_fallback.h"
#include "bootflag.h"

static const char *TAG = "SYSCOORD";

/* ---- state ---- */
static _Atomic sc_mode_t g_mode = SC_MODE_STARTUP;
static _Atomic bool g_tcp_authed = false;

/* ---- internal events & worker ---- */
typedef enum { SYS_EVT_ENTER_RECOVERY } sys_evt_t;
static QueueHandle_t s_sys_q   = NULL;
static TaskHandle_t  s_sys_task = NULL;

/* ---- helpers ---- */
static const char* _st_name(esp_ota_img_states_t st) {
    switch (st) {
        case ESP_OTA_IMG_NEW:             return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY:  return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID:           return "VALID";
        case ESP_OTA_IMG_INVALID:         return "INVALID";
        case ESP_OTA_IMG_ABORTED:         return "ABORTED";
        default:                          return "UNDEFINED";
    }
}

/* ---- fwd decls ---- */
static void set_mode(sc_mode_t m);

/* Mark TCP as authed (called from commands.c after successful auth). */
void syscoord_mark_tcp_authed(void) {
    atomic_store(&g_tcp_authed, true);
}

/* Promote to NORMAL only for TCP + after auth. Also the ONLY place we mark
 * the running image VALID (cancel rollback) once the control path is proven. */
void syscoord_control_path_ok(const char *source) {
    if (!(source && strcmp(source, "TCP") == 0 && atomic_load(&g_tcp_authed))) {
        ESP_LOGW(TAG, "Ignoring control_path_ok from %s (TCP+auth required).",
                 source ? source : "NULL");
        return;
    }

    /* If we booted a new OTA image (NEW/PENDING_VERIFY), now that TCP+AUTH is
     * confirmed, cancel rollback and mark the image VALID. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK &&
        (st == ESP_OTA_IMG_NEW || st == ESP_OTA_IMG_PENDING_VERIFY)) {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Marked OTA image VALID (cancel rollback): %s", esp_err_to_name(e));
    }

    /* Tell the health monitor that control is proven; then go NORMAL. */
    health_monitor_control_ok(source);
    set_mode(SC_MODE_NORMAL);
}

/* Heavy BLE work must run in a normal task, not in a timer callback. */
static void syscoord_worker(void *arg) {
    sys_evt_t ev;
    for (;;) {
        if (xQueueReceive(s_sys_q, &ev, portMAX_DELAY) == pdTRUE) {
            switch (ev) {
            case SYS_EVT_ENTER_RECOVERY:
                /* Bring up BLE and start lifeboat advertising. */
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
        /* Hide BLE while waiting for control; (re)arm event-driven monitor. */
        ble_lifeboat_set(false);
        ble_fallback_stop();
        health_monitor_start(0);  // event-driven escalation only
        ESP_LOGI(TAG, "Health monitor armed (event-driven, no window).");
        break;

    case SC_MODE_NORMAL:
        /* Control path proven (Wi-Fi up + TCP authed). */
        ble_lifeboat_set(false);
        ble_fallback_stop();
        /* Clear post-rollback stickiness ONLY once we’re truly healthy. */
        bootflag_set_post_rollback(false);
        break;

    case SC_MODE_RECOVERY:
        /* BLE bring-up happens in worker after monitor escalation. */
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

    /* Log current running partition + state for clarity. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run) (void)esp_ota_get_state_partition(run, &st);
    ESP_LOGI(TAG, "Running: label=%s subtype=%d off=0x%06x size=%u state=%s",
             run ? run->label : "?",
             run ? run->subtype : -1,
             run ? (unsigned)run->address : 0,
             run ? (unsigned)run->size : 0,
             _st_name(st));

    /* Keep post-rollback sticky until control_path_ok -> NORMAL clears it. */

    /* Start worker before subscribe. */
    s_sys_q = xQueueCreate(4, sizeof(sys_evt_t));
    xTaskCreate(syscoord_worker, "syscoord.wkr", 4096, NULL, 5, &s_sys_task);

    /* Subscribe: monitor will call us when escalation is needed. */
    monitor_set_timeout_cb(on_second_failure);

    /* Begin in WAIT_CONTROL; Wi-Fi events or monitor decide the next step. */
    set_mode(SC_MODE_WAIT_CONTROL);
}

void syscoord_on_wifi_state(bool up) {
    ESP_LOGI(TAG, "Wi-Fi: %s", up ? "UP" : "DOWN");
    if (!up) {
        /* Require re-auth on next session and, if we were NORMAL, return to waiting. */
        atomic_store(&g_tcp_authed, false);
        if (atomic_load(&g_mode) == SC_MODE_NORMAL) {
            set_mode(SC_MODE_WAIT_CONTROL);
        }
    }
    /* BLE is only enabled via worker after monitor failure. */
}

void syscoord_on_tcp_clients(int count) { (void)count; }
void syscoord_on_ble_state(bool connected) { (void)connected; }

void syscoord_on_no_control_path(void) {
    /* Manual escalation -> go through worker. */
    const sys_evt_t ev = SYS_EVT_ENTER_RECOVERY;
    if (s_sys_q) (void)xQueueSend(s_sys_q, &ev, 0);
}

sc_mode_t syscoord_get_mode(void) {
    return atomic_load(&g_mode);
}
