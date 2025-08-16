#pragma once

#include "freertos/FreeRTOS.h"    // included first.
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

// Command types used across tasks via the internal message queue.
typedef enum {
    CMD_LED_ON,       // Turn LED on.
    CMD_LED_OFF,      // Turn LED off.
    CMD_OTA_START     // OTA request (handled by TCP task, forwarded for clarity).
} cmd_t;

void cmd_bus_init(void);

BaseType_t cmd_bus_send(cmd_t cmd, TickType_t ticks);

BaseType_t cmd_bus_receive(cmd_t *out, TickType_t ticks);
