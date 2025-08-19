#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include "command.h"   // concrete cmd_ctx_t
#include "commands.h"

void *cmd_stream_user(cmd_ctx_t *ctx) {
    return ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd;
}

void cmd_reply(cmd_ctx_t *ctx, const char *s) {
    ctx->write(s, (int)strlen(s), cmd_stream_user(ctx));
}

void cmd_replyf(cmd_ctx_t *ctx, const char *fmt, ...) {
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[sizeof(buf)-1] = '\0';
    cmd_reply(ctx, buf);
}
