#include <strings.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "commands.h"
#include "command.h"
#include "app_cfg.h"

/* Remove trailing whitespace, nul-terminating as we trim. */
static size_t rtrim_n(char *s, size_t len) {
    while (len && (unsigned char)s[len - 1] <= ' ')
        s[--len] = '\0';
    return len;
}

/* Skip leading whitespace; updates len to remaining length. */
static char *lskip_n(char *s, size_t *len) {
    size_t i = 0;
    while (i < *len && (unsigned char)s[i] <= ' ')
        i++;
    *len -= i;
    return s + i;
}

void cmd_dispatch_line(char *line, size_t len, cmd_ctx_t *ctx) {
    if (!line || !ctx) return;

    /* Trim whole line. */
    len  = rtrim_n(line, len);
    line = lskip_n(line, &len);
    if (!len) return; /* empty */

    /* Extract command token. */
    size_t cmd_len = 0;
    while (cmd_len < len && (unsigned char)line[cmd_len] > ' ')
        cmd_len++;

    /* Extract + trim args (if any). */
    char  *args = NULL;
    size_t args_len = 0;
    if (cmd_len < len) {
        args_len = len - cmd_len;
        args     = lskip_n(line + cmd_len, &args_len);
        args_len = rtrim_n(args, args_len);
    }

    /* Lookup in command table. */
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (CMDS[i].name_len == cmd_len &&
            strncasecmp(line, CMDS[i].name, cmd_len) == 0) {

            if (CMDS[i].needs_auth && !ctx->authed) {
                cmd_reply(ctx, "DENIED\n");
                return;
            }

            CMDS[i].fn((args && args_len) ? args : "", ctx);
            return;
        }
    }

    /* Not found. */
    cmd_reply(ctx, "WHAT\n");
}
