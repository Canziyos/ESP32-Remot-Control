#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef int (*cmd_write_fn)(const void *buffer, int len, void *user);

/* Give the struct a tag so others can forward‑declare it */
typedef struct cmd_ctx_t {
    bool authed;
    bool is_ble;
    int  tcp_fd;
    void *ble_link;
    cmd_write_fn write;
} cmd_ctx_t;

/* Dispatch API */
void cmd_dispatch_line(char *line, size_t len, cmd_ctx_t *ctx);
