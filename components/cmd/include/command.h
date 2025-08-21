// command.h
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cmd_write_fn)(const void *buffer, size_t len, void *user);

typedef enum {
    CMD_XPORT_TCP = 1,
    CMD_XPORT_BLE = 2,
} cmd_xport_t;

typedef struct cmd_ctx_t {
    bool authed;
    cmd_xport_t xport;
    union {
        int   tcp_fd;
        void *ble_link;
    } u;
    cmd_write_fn write;
} cmd_ctx_t;

void cmd_dispatch_line(char *line, size_t len, cmd_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
