// ble_cmd.c
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gatts_api.h"

#include "ble_cmd.h"
#include "command.h"     // cmd_ctx_t, cmd_dispatch_line
#include "commands.h"    // command table & needs_auth flags
#include "gatt_server.h" // gatt_server_send_status()

#define BLE_CMD_LINE_MAX 128

struct ble_cmd {
    esp_gatt_if_t ifx;
    uint16_t      conn_id;
    uint16_t      tx_handle;                // efbe0200… (notify/read) char handle
    char          line[BLE_CMD_LINE_MAX];
    size_t        len;

    cmd_ctx_t     ctx;                      // persisted across commands (keeps .authed)
};

static const char *TAG = "BLE_CMD";

/* Write hook used by the command layer to send replies over BLE (via GATT helper). */
static int ble_cmd_write_cb(const void *buf, int n, void *user) {
    (void)user;
    if (!buf || n <= 0) return 0;

    // Ensure it's a C-string for gatt_server_send_status().
    // Most replies are short; copy into a small stack buffer if needed.
    if (((const char*)buf)[n - 1] == '\0') {
        gatt_server_send_status((const char*)buf);
    } else {
        char tmp[BLE_CMD_LINE_MAX + 2];
        size_t m = (n > (int)BLE_CMD_LINE_MAX) ? BLE_CMD_LINE_MAX : (size_t)n;
        memcpy(tmp, buf, m);
        tmp[m] = '\0';
        gatt_server_send_status(tmp);
    }
    return n;
}

ble_cmd_t* ble_cmd_create(void) {
    ble_cmd_t *cli = (ble_cmd_t*)calloc(1, sizeof(*cli));
    return cli;
}

void ble_cmd_destroy(ble_cmd_t* cli) {
    if (cli) free(cli);
}

void ble_cmd_on_connect(ble_cmd_t* cli,
                        esp_gatt_if_t gatts_if,
                        uint16_t conn_id,
                        uint16_t tx_char_handle)
{
    if (!cli) return;
    memset(cli, 0, sizeof(*cli));

    cli->ifx       = gatts_if;
    cli->conn_id   = conn_id;
    cli->tx_handle = tx_char_handle;

    // Initialize command context to mirror TCP behavior:
    cli->ctx.authed   = false;          // start locked down (auth required)
    cli->ctx.is_ble   = true;           // mark BLE path
    cli->ctx.tcp_fd   = -1;
    cli->ctx.ble_link = cli;            // opaque backref if you ever need it
    cli->ctx.write    = ble_cmd_write_cb;

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

        // End of one line -> dispatch
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
