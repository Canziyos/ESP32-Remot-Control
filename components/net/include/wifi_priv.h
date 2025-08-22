// wifi_priv.h
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_event.h" 
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Backoff subsystem (internal) */
void wifi_backoff_reset(void);
void wifi_backoff_schedule(void);
void wifi_backoff_stop_timer(void);

/* TCP start timer helpers (internal) */
void wifi_tcp_timer_create_once(void);
bool wifi_tcp_timer_is_active(void);
void wifi_tcp_timer_stop_if_active(void);
void wifi_tcp_timer_schedule_us(uint64_t usec);

/* Event handlers implemented in wifi_event.c */
void got_ip(void *arg, esp_event_base_t base, int32_t id, void *data);
void got_ip_lost(void *arg, esp_event_base_t base, int32_t id, void *data);
void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data);

#ifdef __cplusplus
}
#endif
