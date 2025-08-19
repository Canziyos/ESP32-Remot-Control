#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declare the tagged struct from command.h */
struct cmd_ctx_t;

/* Handler signature */
typedef void (*cmd_fn_t)(const char *args, struct cmd_ctx_t *ctx);

/* Table entry */
typedef struct {
    const char *name;     /* lowercase keyword */
    size_t      name_len; /* precomputed strlen(name) */
    bool        needs_auth;
    cmd_fn_t    fn;
} cmd_entry_t;

/* Command table */
extern const cmd_entry_t CMDS[];
extern const size_t      CMD_COUNT;

/* Shared reply helpers (implemented in cmd_reply.c) */
void *cmd_stream_user(struct cmd_ctx_t *ctx);
void  cmd_reply(struct cmd_ctx_t *ctx, const char *s);
void  cmd_replyf(struct cmd_ctx_t *ctx, const char *fmt, ...);

/* Handlers can keep calling reply()/replyf() */
#define reply  cmd_reply
#define replyf cmd_replyf

#ifdef __cplusplus
}
#endif
