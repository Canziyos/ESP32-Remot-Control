// priv/ble_priv.h
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_gap_ble_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Events -> worker */
typedef enum {
  BLE_EVT_ADV_KICK = 1
} ble_evt_t;

/* Worker/thread plumbing (owned by fb_worker.c) */
extern QueueHandle_t s_ble_q;
extern TaskHandle_t  s_ble_wkr;
void  fb_worker_init_once(void);
void  ble_post(ble_evt_t ev);

/* Shared state (owned by fb_core.c) */
extern bool s_stack_ready;
extern bool s_adv_ready;
extern bool s_adv_running;
extern bool s_connected;
extern bool s_lifeboat_enabled;
extern bool s_adv_start_deferred;

extern TimerHandle_t s_adv_watch;

extern uint8_t  s_adv_uuid[16];
extern uint8_t  s_adv_cfg_done;          // bitmask
extern esp_ble_adv_params_t s_adv_params;

/* Actions (implemented in fallback) */
void fb_adv_kick(void);                   // start advertising if policy allows
void fb_gap_evt(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p);

/* watchdog helpers */
void fb_watchdog_start_if_needed(void);
void fb_watchdog_stop(void);

/* Bridge for public stop */
void fb_adv_stop_public(void);

#ifdef __cplusplus
}
#endif
