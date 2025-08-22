#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmd_ctx_t cmd_ctx_t;

typedef enum {
    CMD_NONE = 0,
    CMD_LED_ON,
    CMD_LED_OFF,
    CMD_OTA_START,
    CMD_DHT_QUERY,
    CMD_DHT_STREAM_ON,
    CMD_DHT_STREAM_OFF,
    CMD_DHT_STATE,
} cmd_t;

typedef struct {
    cmd_t      cmd;
    cmd_ctx_t *ctx;   // originator to reply to (may be NULL)
    uint32_t   u32;   // optional numeric arg
} cmd_msg_t;

void cmd_bus_init(void);
bool cmd_bus_is_ready(void);

BaseType_t cmd_bus_send(cmd_t cmd, TickType_t ticks);
BaseType_t cmd_bus_receive(cmd_t *out, TickType_t ticks);

BaseType_t cmd_bus_send_msg(const cmd_msg_t *msg, TickType_t ticks);
BaseType_t cmd_bus_receive_msg(cmd_msg_t *out, TickType_t ticks);

#ifdef __cplusplus
}
#endif
