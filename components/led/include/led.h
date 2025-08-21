// components/led/include/led.h
#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t led_init(void);
void led_on(void);
void led_off(void);
#ifdef __cplusplus
}
#endif
