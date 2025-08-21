// command_bus.h
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CMD_NONE = 0,
    CMD_LED_ON,
    CMD_LED_OFF,
    CMD_OTA_START
} cmd_t;

void cmd_bus_init(void);
BaseType_t cmd_bus_send(cmd_t cmd, TickType_t ticks);
BaseType_t cmd_bus_receive(cmd_t *out, TickType_t ticks);
/* let callers check if init happened. */
bool cmd_bus_is_ready(void);

#ifdef __cplusplus
}
#endif
