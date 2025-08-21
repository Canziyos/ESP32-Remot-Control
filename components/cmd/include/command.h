// command.h
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cmd_write_fn)(const void *buffer, size_t len, void *user);

typedef enum {
    CMD_XPORT_NONE = 0,
    CMD_XPORT_TCP  = 1,
    CMD_XPORT_BLE  = 2,
} cmd_xport_t;

typedef struct cmd_ctx_t {
    bool authed;
    cmd_xport_t xport;
    union {
        int tcp_fd;    // valid when xport==CMD_XPORT_TCP
        void *ble_link;  // valid when xport==CMD_XPORT_BLE
    } u;
    cmd_write_fn write;  // must be set by transport
} cmd_ctx_t;

// initializers.
#define CMD_CTX_INIT_TCP(fd, write_fn) ((cmd_ctx_t){ \
    .authed=false, .xport=CMD_XPORT_TCP, .u.tcp_fd=(fd), .write=(write_fn) })

#define CMD_CTX_INIT_BLE(link, write_fn) ((cmd_ctx_t){ \
    .authed=false, .xport=CMD_XPORT_BLE, .u.ble_link=(link), .write=(write_fn) })

void cmd_dispatch_line(char *line, size_t len, cmd_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
