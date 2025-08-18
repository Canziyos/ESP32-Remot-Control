#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "commands.h"
#include "command.h"
#include "app_cfg.h"

// Remove trailing whitespace.
static size_t rtrim_n(char *s, size_t len) {
    while (len && (unsigned char)s[len - 1] <= ' ')
        s[--len] = '\0';
    return len;
}


// Skip leading whitespace.
static char *lskip_n(char *s, size_t *len) {
    size_t i = 0;
    while (i < *len && (unsigned char)s[i] <= ' ')
        i++;
    *len -= i;
    return s + i;
}

void cmd_dispatch_line(char *line, size_t len, cmd_ctx_t *ctx) {
    // Trim whitespace.
    len = rtrim_n(line, len);
    line = lskip_n(line, &len);
    if (!len) return; // empty

    // Extract command name.
    size_t cmd_len = 0;
    while (cmd_len < len && (unsigned char)line[cmd_len] > ' ')
        cmd_len++;

    // Extract args if any.
    char *args = NULL;
    if (cmd_len < len) {
        args = lskip_n(line + cmd_len, &(size_t){ len - cmd_len });
    }

    // Lookup in command table.
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (CMDS[i].name_len == cmd_len &&
            strncasecmp(line, CMDS[i].name, cmd_len) == 0) {

            if (CMDS[i].needs_auth && !ctx->authed) {
                ctx->write("DENIED\n", 7,
                           ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd);
                return;
            }

            CMDS[i].fn(args ? args : "", ctx);
            return;
        }
    }

    // Not found.
    ctx->write("WHAT\n", 5,
               ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd);
}
