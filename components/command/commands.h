#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Forward-declare the tagged struct from command.h */
struct cmd_ctx_t;

typedef void (*cmd_fn_t)(const char *args, struct cmd_ctx_t *ctx);

typedef struct {
    const char *name;      // lowercase command keyword
    size_t      name_len;  // precomputed strlen(name)
    bool        needs_auth;
    cmd_fn_t    fn;
} cmd_entry_t;

/* Command table (defined in commands.c) */
extern const cmd_entry_t CMDS[];
extern const size_t      CMD_COUNT;
