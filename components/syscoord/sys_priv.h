// nternal structs/helpers (not exported).

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "syscoord.h"
#include "monitor.h"
#include "alerts.h"
#include "ble_fallback.h"
#include "bootflag.h"

/* ---- shared state (defined in sys_core.c) ---- */
extern _Atomic sc_mode_t g_mode;
extern _Atomic bool g_tcp_authed;

extern QueueHandle_t s_sys_q;
extern TaskHandle_t s_sys_task;

extern const char *SYSCOORD_TAG;

/* ---- internal worker/event plumbing (used across units) ---- */
typedef enum {
  SYS_EVT_ENTER_RECOVERY = 1
} sys_evt_t;

void set_mode(sc_mode_t m);            /* defined in sys_core.c */
void syscoord_worker(void *arg);       /* defined in sys_policy.c */

/* helper to enqueue recovery request */
static inline void enqueue_recovery(void) {
  const sys_evt_t ev = SYS_EVT_ENTER_RECOVERY;
  if (s_sys_q) (void)xQueueSend(s_sys_q, &ev, 0);
}

/* tiny utl */
static inline const char* _ota_st_name(esp_ota_img_states_t st) {
  switch (st) {
    case ESP_OTA_IMG_NEW: return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID: return "VALID";
    case ESP_OTA_IMG_INVALID: return "INVALID";
    case ESP_OTA_IMG_ABORTED: return "ABORTED";
    default: return "UNDEFINED";
  }
}
