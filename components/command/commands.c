#include <string.h>
#include <stdio.h>
#include <stdint.h>          // intptr_t cast for TCP fd
#include "command.h"         // cmd_ctx_t
#include "commands.h"
#include "syscoord.h"
#include "ota_handler.h"
#include "command_bus.h"
#include "esp_log.h"
#include "errsrc.h"
#include "wifi.h"            // wifi_set_credentials

/* ---- helpers ---- */
static inline void *stream_user(cmd_ctx_t *ctx) {
    return ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd;
}
static inline void reply(cmd_ctx_t *ctx, const char *s) {
    ctx->write(s, (int)strlen(s), stream_user(ctx));
}

static const char* mode_to_str(sc_mode_t m) {
    switch (m) {
        case SC_MODE_WAIT_CONTROL: return "WAIT_CONTROL";
        case SC_MODE_NORMAL:       return "NORMAL";
        case SC_MODE_RECOVERY:     return "RECOVERY";
        default:                   return "STARTUP";
    }
}

/* ---- Command Handlers ---- */

static void cmd_ping(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    reply(ctx, "PONG\n");
}

static void cmd_auth(const char *args, cmd_ctx_t *ctx) {
    // TODO: replace "hunter2" with NVS-backed token check if needed
    if (strcmp(args, "hunter2") == 0) {
        ctx->authed = true;
        // Only TCP/Wi-Fi establishes the control path (cancels rollback, hides BLE).
        if (!ctx->is_ble) {
            syscoord_mark_tcp_authed();
            syscoord_control_path_ok("TCP");
        }
        reply(ctx, "OK\n");
    } else {
        reply(ctx, "DENIED\n");
    }
}

static void cmd_led_on(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    cmd_bus_send(CMD_LED_ON, 0);
    reply(ctx, "led_on\n");
}

static void cmd_led_off(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    cmd_bus_send(CMD_LED_OFF, 0);
    reply(ctx, "led_off\n");
}

static void cmd_version(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    reply(ctx, "1.0.0\n");
}

/* OTA <size> <crc>
 * NOTE: Do NOT send ACK here — ota_perform() sends "ACK\n" itself. */
static void cmd_ota(const char *args, cmd_ctx_t *ctx) {
    uint32_t sz = 0, crc_ignored = 0;
    if (sscanf(args, "%u %x", (unsigned int*)&sz, (unsigned int*)&crc_ignored) == 2 && sz > 0) {
        if (!ctx->is_ble && ctx->tcp_fd >= 0) {
            (void)ota_perform(ctx->tcp_fd, sz);    // reboots on success; handles replies
        } else {
            reply(ctx, "OTA_UNSUPPORTED\n");
        }
    } else {
        reply(ctx, "BADFMT\n");
    }
}

static void cmd_setwifi(const char *args, cmd_ctx_t *ctx) {
    char ssid[33] = {0}, pwd[65] = {0};
    if (sscanf(args, "%32s %64s", ssid, pwd) == 2) {
        wifi_set_credentials(ssid, pwd);          // triggers reconnect
        reply(ctx, "WIFI_UPDATED\n");
    } else {
        reply(ctx, "BADFMT\n");
    }
}

static void cmd_errsrc(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    const char *err = errsrc_get(); if (!err) err = "NONE";
    sc_mode_t m = syscoord_get_mode();

    char line[96];
    (void)snprintf(line, sizeof(line), "mode=%s errsrc=%s\n", mode_to_str(m), err);
    reply(ctx, line);
}

/* ---- Command Table ---- */
#define CMD(name, auth, fn) { (name), sizeof(name)-1, (auth), (fn) }

const cmd_entry_t CMDS[] = {
    CMD("ping",    false, cmd_ping),
    CMD("auth",    false, cmd_auth),
    CMD("led_on",  true,  cmd_led_on),
    CMD("led_off", true,  cmd_led_off),
    CMD("version", false, cmd_version),
    CMD("ota",     true,  cmd_ota),       // TCP-only; handler guards
    CMD("setwifi", true,  cmd_setwifi),
    CMD("errsrc",  false, cmd_errsrc),
};
const size_t CMD_COUNT = sizeof(CMDS) / sizeof(CMDS[0]);
