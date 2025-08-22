// components/ble/gatt/ble_cmd.c
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gatts_api.h"
#include "gatt_priv.h"       // ble_cmd_t forward-decl + internal APIs
#include "gatt_server.h"     // gatt_server_send_status()
#include "command.h"         // cmd_ctx_t, cmd_dispatch_line
#include "commands.h"        // command table & needs_auth flags
#include <limits.h>

static const char *TAG = "BLE_CMD";

#define BLE_CMD_LINE_MAX 128

struct ble_cmd {
    esp_gatt_if_t ifx;
    uint16_t conn_id;
    uint16_t tx_handle;                // efbe0200… (notify/read) char handle
    char line[BLE_CMD_LINE_MAX];
    size_t len;
    cmd_ctx_t ctx;               // persisted across commands (keeps .authed)
};



static int ble_cmd_write_cb(const void *buf, size_t n, void *user) {
    (void)user;
    if (!buf || n == 0) return 0;

    const char *p = (const char*)buf;

    // Trim one trailing newline so we don't end up with double '\n' from gatt_server_send_status
    size_t use_n = n;
    if (use_n && p[use_n - 1] == '\n') use_n--;

    if (use_n && p[use_n - 1] == '\0') {
        gatt_server_send_status(p);
    } else {
        char tmp[BLE_CMD_LINE_MAX + 2];
        size_t m = use_n > (size_t)BLE_CMD_LINE_MAX ? (size_t)BLE_CMD_LINE_MAX : use_n;
        memcpy(tmp, p, m);
        tmp[m] = '\0';
        gatt_server_send_status(tmp);
    }
    return (int)(use_n > (size_t)INT_MAX ? INT_MAX : use_n);
}


ble_cmd_t* ble_cmd_create(void) {
    ble_cmd_t *cli = (ble_cmd_t*)calloc(1, sizeof(*cli));
    return cli;
}

void ble_cmd_destroy(ble_cmd_t* cli) {
    if (cli) free(cli);
}

void ble_cmd_on_connect(ble_cmd_t* cli, esp_gatt_if_t gatts_if,
                        uint16_t conn_id, uint16_t tx_char_handle)
{
    if (!cli) return;
    memset(cli, 0, sizeof(*cli));

    cli->ifx = gatts_if;
    cli->conn_id = conn_id;
    cli->tx_handle = tx_char_handle;

    cli->ctx.authed = false;
    cli->ctx.xport = CMD_XPORT_BLE;
    cli->ctx.u.ble_link = cli;     // opaque backref if needed.
    cli->ctx.write  = ble_cmd_write_cb;

    ESP_LOGI(TAG, "BLE CMD connected (conn_id=%u, tx_handle=0x%04x).",
             (unsigned)conn_id, (unsigned)tx_char_handle);
}

void ble_cmd_on_rx(ble_cmd_t* cli, const uint8_t* data, uint16_t len) {
    if (!cli || !data || len == 0) return;

    for (uint16_t i = 0; i < len; ++i) {
        uint8_t c = data[i];
        if (c == '\r') continue;

        if (c != '\n') {
            if (cli->len < sizeof(cli->line) - 1) {
                cli->line[cli->len++] = (char)c;
            }
            continue;
        }

        // End of one line -> dispatch.
        cli->line[cli->len] = '\0';
        if (cli->len) {
            cmd_dispatch_line(cli->line, cli->len, &cli->ctx);
        }
        cli->len = 0;
    }
}

void ble_cmd_on_disconnect(ble_cmd_t* cli) {
    if (!cli) return;
    cli->len        = 0;
    cli->ctx.authed = false; // drop auth on link loss to mirror TCP lifecycle
    ESP_LOGI(TAG, "BLE CMD disconnected (conn_id=%u).", (unsigned)cli->conn_id);
}
