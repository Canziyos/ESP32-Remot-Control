//include/blefallback.h
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lifeboat / recovery BLE control (called by syscoord). */
void ble_fallback_init(void);        /* bring up BLE stack if needed */
void ble_fallback_stop(void);        /* tear down / stop advertising */
void ble_lifeboat_set(bool on);      /* enable/disable recovery advertising */
void ble_start_advertising(void);    /* kick advertising when GATT is ready */

#ifdef __cplusplus
}
#endif
