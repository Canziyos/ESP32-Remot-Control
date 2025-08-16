#include "command_bus.h"

// Internal command queue used to pass messages between tasks.
static QueueHandle_t _q;

// Creating the internal queue (Initializes the command bus by).
// The queue can hold up to 8 command elements.
void cmd_bus_init(void) {
    _q = xQueueCreate(8, sizeof(cmd_t));
}

// Sends a command to the bus.
// Blocks for, t, ticks if the queue is full.
BaseType_t cmd_bus_send(cmd_t cmd, TickType_t t) {
    return xQueueSend(_q, &cmd, t);
}

// Receives a command from the bus.
// Blocks for, t, ticks if the queue empty.
BaseType_t cmd_bus_receive(cmd_t *o, TickType_t t) {
    return xQueueReceive(_q, o, t);
}
