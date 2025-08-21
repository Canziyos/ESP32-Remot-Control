#include <stdio.h>
#include <stdint.h>
#include "commands.h"
#include "command.h"
#include "ota_handler.h"

id cmd_ota(const char *args, cmd_ctx_t *ctx) {
    if (!args || !*args) { cmd_reply(ctx, "BADFMT\n"); return; }

    unsigned size_u = 0, crc_u = 0;
    if (sscanf(args, "%u %x", &size_u, &crc_u) == 2 && size_u > 0) {
        if (ctx->xport == CMD_XPORT_TCP && ctx->u.tcp_fd >= 0) {
            (void)ota_perform(ctx->u.tcp_fd, (uint32_t)size_u);
        } else {
            cmd_reply(ctx, "OTA_UNSUPPORTED\n");
        }
    } else {
        cmd_reply(ctx, "BADFMT\n");
    }
}