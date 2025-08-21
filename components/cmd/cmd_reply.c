#include "commands.h"
#include "command.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>   // intptr_t

void *cmd_stream_user(cmd_ctx_t *ctx) {
    if (!ctx) return NULL;
    switch (ctx->xport) {
        case CMD_XPORT_BLE: return ctx->u.ble_link;
        case CMD_XPORT_TCP: return (void*)(intptr_t)ctx->u.tcp_fd;
        default: return NULL;
    }
}

void cmd_reply(cmd_ctx_t *ctx, const char *s) {
    if (!ctx || !ctx->write || !s) return;
    size_t n = strlen(s);
    (void)ctx->write(s, n, cmd_stream_user(ctx));
}

void cmd_replyf(cmd_ctx_t *ctx, const char *fmt, ...) {
    char buf[320];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[sizeof(buf)-1] = '\0';
    cmd_reply(ctx, buf);
}
