// commands.h
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "command.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef void (*cmd_fn_t)(const char *args, cmd_ctx_t *ctx);

typedef struct {
    const char *name;
    size_t    name_len;   /* precomputed, O(n) scan without strlen. */
    bool        needs_auth;
    cmd_fn_t    fn;
} cmd_entry_t;

extern const cmd_entry_t CMDS[];
extern const size_t CMD_COUNT;
/* Optional: helper to find an entry (implemented in cmd_table.c). */
const cmd_entry_t* cmd_find(const char *cmd, size_t n);

/* Implemented in cmd_reply.c */
void *cmd_stream_user(cmd_ctx_t *ctx);
void  cmd_reply(cmd_ctx_t *ctx, const char *s);
void  cmd_replyf(cmd_ctx_t *ctx, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
