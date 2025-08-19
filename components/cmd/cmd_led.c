#include "commands.h"
#include "command_bus.h"

void cmd_led_on(const char *args, cmd_ctx_t *ctx){ (void)args; cmd_bus_send(CMD_LED_ON,0);  reply(ctx,"led_on\n"); }
void cmd_led_off(const char *args, cmd_ctx_t *ctx){ (void)args; cmd_bus_send(CMD_LED_OFF,0); reply(ctx,"led_off\n"); }
