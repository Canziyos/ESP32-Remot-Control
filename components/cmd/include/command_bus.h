#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declare the CLI context to avoid heavy includes here. */
typedef struct cmd_ctx_t cmd_ctx_t;

typedef enum {
    CMD_NONE = 0,
    CMD_LED_ON,
    CMD_LED_OFF,
    CMD_OTA_START,
    CMD_DHT_QUERY,
    CMD_DHT_STREAM_ON,
    CMD_DHT_STREAM_OFF,
    CMD_DHT_STATE,          /* `dhtstate` also goes via the bus */
} cmd_t;

/* New: full message that can carry origin ctx + one numeric arg (e.g., ms). */
typedef struct {
    cmd_t      cmd;      /* which command. */
    cmd_ctx_t *ctx;      /* originator context to reply to (NULL if no reply). */
    uint32_t   u32;      /* numeric parameter (e.g., interval). */
} cmd_msg_t;

/* Init + readiness */
void cmd_bus_init(void);
bool cmd_bus_is_ready(void);

/* still supported (no context, no args). */
BaseType_t cmd_bus_send(cmd_t cmd, TickType_t ticks);
BaseType_t cmd_bus_receive(cmd_t *out, TickType_t ticks);

/* New, reply-capable message APIs. */
BaseType_t cmd_bus_send_msg(const cmd_msg_t *msg, TickType_t ticks);
BaseType_t cmd_bus_receive_msg(cmd_msg_t *out, TickType_t ticks);

#ifdef __cplusplus
}
#endif
