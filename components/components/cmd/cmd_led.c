#include "command.h"
#include "command_bus.h"
#include "commands.h"
// if pdMS_TO_TICKS used, then below:
//#include "freertos/FreeRTOS.h"

void cmd_led_on(const char *args, cmd_ctx_t *ctx){
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    if (cmd_bus_send(CMD_LED_ON, pdMS_TO_TICKS(20)) == pdTRUE)
        cmd_reply(ctx, "LED_ON\n");
    else
        cmd_reply(ctx, "BUS_FULL\n");
}

void cmd_led_off(const char *args, cmd_ctx_t *ctx){
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    if (cmd_bus_send(CMD_LED_OFF, pdMS_TO_TICKS(20)) == pdTRUE)
        cmd_reply(ctx, "LED_OFF\n");
    else
        cmd_reply(ctx, "BUS_FULL\n");
}
