#include "command_bus.h"

#define CMD_BUS_DEPTH 8
// Internal command queue used to pass messages between tasks.
static QueueHandle_t _q;

// Creating the internal queue (Initializes the command bus by).
// The queue can hold up to 8 command elements.
void cmd_bus_init(void) {
    if (_q) return;
    _q = xQueueCreate(CMD_BUS_DEPTH, sizeof(cmd_t));
}

// Sends a command to the bus.
// Blocks for, t, ticks if the queue is full.
BaseType_t cmd_bus_send(cmd_t cmd, TickType_t t) {
    if (!_q) return pdFALSE;
    return xQueueSend(_q, &cmd, t);
}

// Receives a command from the bus.
// Blocks for, t, ticks if the queue empty.
BaseType_t cmd_bus_receive(cmd_t *o, TickType_t t) {
    if (!_q || !o) return pdFALSE;
    return xQueueReceive(_q, o, t);
}

bool cmd_bus_is_ready(void) {
    return _q != NULL;
}
