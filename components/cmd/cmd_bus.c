#include "command_bus.h"

#define CMD_BUS_DEPTH 16

static QueueHandle_t s_q = NULL;

void cmd_bus_init(void) {
    if (s_q) return;
    // Queue stores full cmd_msg_t so we carry ctx/u32 to the router.
    s_q = xQueueCreate(CMD_BUS_DEPTH, sizeof(cmd_msg_t));
}

bool cmd_bus_is_ready(void) {
    return s_q != NULL;
}

/* Rich message APIs. */
BaseType_t cmd_bus_send_msg(const cmd_msg_t *msg, TickType_t ticks) {
    if (!s_q || !msg) return pdFALSE;
    return xQueueSend(s_q, msg, ticks);
}

BaseType_t cmd_bus_receive_msg(cmd_msg_t *out, TickType_t ticks) {
    if (!s_q || !out) return pdFALSE;
    return xQueueReceive(s_q, out, ticks);
}

/* Legacy wrappers (no ctx/arg) */
BaseType_t cmd_bus_send(cmd_t cmd, TickType_t ticks) {
    cmd_msg_t m = { .cmd = cmd, .ctx = NULL, .u32 = 0 };
    return cmd_bus_send_msg(&m, ticks);
}

BaseType_t cmd_bus_receive(cmd_t *out, TickType_t ticks) {
    cmd_msg_t m;
    if (cmd_bus_receive_msg(&m, ticks) != pdTRUE) return pdFALSE;
    if (out) *out = m.cmd;
    return pdTRUE;
}
