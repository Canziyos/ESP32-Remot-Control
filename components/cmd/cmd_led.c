#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "command_bus.h"
#include "commands.h"
#include "led.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define LED_GPIO 22  // GPIO pin connected to the LED.

void cmd_led_on(const char *args, cmd_ctx_t *ctx){
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    cmd_msg_t m = { .cmd = CMD_LED_ON, .ctx = ctx, .u32 = 0 };
    if (cmd_bus_send_msg(&m, pdMS_TO_TICKS(20)) != pdTRUE) {
        cmd_reply(ctx, "BUS_FULL\n");
    }
    /* removed immediate reply fromhere; the router will reply "LED_ON". */
}

void cmd_led_off(const char *args, cmd_ctx_t *ctx){
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    cmd_msg_t m = { .cmd = CMD_LED_OFF, .ctx = ctx, .u32 = 0 };
    if (cmd_bus_send_msg(&m, pdMS_TO_TICKS(20)) != pdTRUE) {
        cmd_reply(ctx, "BUS_FULL\n");
    }
    /* the same as above */
}
