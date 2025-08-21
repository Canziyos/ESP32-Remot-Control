#include "commands.h"
#include "command_bus.h"
#include <string.h>
#include <stdlib.h>

/* DHT? => enqueue a query; the DHT task will reply via ctx. */
void cmd_dht(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    cmd_msg_t m = { .cmd = CMD_DHT_QUERY, .ctx = ctx, .u32 = 0 };
    if (cmd_bus_send_msg(&m, pdMS_TO_TICKS(50)) != pdTRUE)
        cmd_reply(ctx, "BUS_FULL\n");
}

/* DHTSTREAM on/off [ms] => enable/disable streaming via bus. */
void cmd_dhtstream(const char *args, cmd_ctx_t *ctx) {
    if (!ctx->authed) { cmd_reply(ctx, "DENIED\n"); return; }
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    if (!args || !*args) { cmd_reply(ctx, "usage: DHTSTREAM on|off [ms]\n"); return; }

    char opt[16] = {0};
    unsigned interval = 0;
    int n = sscanf(args, "%15s %u", opt, &interval);
    if (n < 1) { cmd_reply(ctx, "usage: DHTSTREAM on|off [ms]\n"); return; }

    cmd_msg_t m = { .ctx = ctx, .u32 = interval };
    if (strcasecmp(opt, "on") == 0)       m.cmd = CMD_DHT_STREAM_ON;
    else if (strcasecmp(opt, "off") == 0) m.cmd = CMD_DHT_STREAM_OFF;
    else { cmd_reply(ctx, "usage: DHTSTREAM on|off [ms]\n"); return; }

    if (cmd_bus_send_msg(&m, pdMS_TO_TICKS(50)) != pdTRUE)
        cmd_reply(ctx, "BUS_FULL\n");
    /* ACK ("DHTSTREAM ON/OFF") will be sent by the DHT task. */
}

/* DHTSTATE => query current stream flag/interval via bus. */
void cmd_dhtstate(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    if (!cmd_bus_is_ready()) { cmd_reply(ctx, "BUS_DOWN\n"); return; }
    cmd_msg_t m = { .cmd = CMD_DHT_STATE, .ctx = ctx, .u32 = 0 };
    if (cmd_bus_send_msg(&m, pdMS_TO_TICKS(50)) != pdTRUE)
        cmd_reply(ctx, "BUS_FULL\n");
}
