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
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "bootflag.h"
#include "esp_partition.h"
#include <stdbool.h>

#include "esp_app_format.h" // esp_app_desc_t



// --- AUTH token in NVS ("auth"/"token") ---
static char s_auth_token[65] = "hunter2";   // default.
static bool s_auth_loaded = false;

static void auth_token_load(void) {
    if (s_auth_loaded) return;
    nvs_handle_t h;
    if (nvs_open("auth", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_auth_token);
        if (nvs_get_str(h, "token", s_auth_token, &len) != ESP_OK) {
            // keep default
        }
        nvs_close(h);
    }
    s_auth_loaded = true;
}

static esp_err_t auth_token_save(const char *tok) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("auth", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "token", tok ? tok : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        strncpy(s_auth_token, tok ? tok : "", sizeof(s_auth_token)-1);
        s_auth_token[sizeof(s_auth_token)-1] = '\0';
        s_auth_loaded = true;
    }
    return err;
}

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

static const char* _ota_state_str(esp_ota_img_states_t st) {
    switch (st) {
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED:
        default: return "UNDEFINED";
    }
}

static void cmd_diag(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    char buf[320];
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run) (void)esp_ota_get_state_partition(run, &st);

    sc_mode_t mode = syscoord_get_mode();
    bool post_rb = bootflag_is_post_rollback();

    snprintf(buf, sizeof(buf),
        "PART=%s SUB=%d OFF=0x%06x SIZE=%u\r\n"
        "OTA_STATE=%s\r\n"
        "POST_RB=%d\r\n"
        "MODE=%s\r\n",
        run ? run->label : "?", run ? run->subtype : -1,
        run ? (unsigned)run->address : 0, run ? (unsigned)run->size : 0,
        _ota_state_str(st),
        post_rb ? 1 : 0,
        mode_to_str(mode));
    reply(ctx, buf);
}


static void cmd_led_on(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    cmd_bus_send(CMD_LED_ON, 0);
    reply(ctx, "led_on\n");
}


/* ---- Command Handlers ---- */

static void cmd_ping(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    reply(ctx, "PONG\n");
}

static void cmd_auth(const char *args, cmd_ctx_t *ctx) {
    auth_token_load();
    if (args && strcmp(args, s_auth_token) == 0) {
        ctx->authed = true;
        if (!ctx->is_ble) {
            syscoord_mark_tcp_authed();
            syscoord_control_path_ok("TCP");
        }
        reply(ctx, "OK\n");
    } else {
        reply(ctx, "DENIED\n");
    }
}

static void cmd_settoken(const char *args, cmd_ctx_t *ctx) {
    if (!ctx->authed) { reply(ctx, "DENIED\n"); return; }
    if (!args || !*args) { reply(ctx, "usage: SETTOKEN <newtoken>\n"); return; }
    if (strlen(args) >= sizeof(s_auth_token)) { reply(ctx, "ERR token too long\n"); return; }
    if (auth_token_save(args) == ESP_OK) {
        reply(ctx, "OK\n");
    } else {
        reply(ctx, "ERR\n");
    }
}


static void cmd_led_off(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    cmd_bus_send(CMD_LED_OFF, 0);
    reply(ctx, "led_off\n");
}

static void cmd_version(const char *args, cmd_ctx_t *ctx) {
    (void)args;
    esp_app_desc_t d = {0};
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && esp_ota_get_partition_description(run, &d) == ESP_OK && d.version[0]) {
        char out[96];
        int n = snprintf(out, sizeof(out), "%s\n", d.version);
        ctx->write(out, n, ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd);
        return;
    }
#ifdef CONFIG_APP_PROJECT_VER
    char out[96];
    int n = snprintf(out, sizeof(out), "%s\n", CONFIG_APP_PROJECT_VER);
    ctx->write(out, n, ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd);
#else
    ctx->write("unknown\n", 8, ctx->is_ble ? ctx->ble_link : (void*)(intptr_t)ctx->tcp_fd);
#endif
}


/* OTA <size> <crc>
 * ota_perform() sends "ACK\n" itself. */
static void cmd_ota(const char *args, cmd_ctx_t *ctx) {
    unsigned size_u = 0, crc_u = 0;
    if (sscanf(args, "%u %x", &size_u, &crc_u) == 2 && size_u > 0) {
        uint32_t sz = (uint32_t)size_u;
        (void)crc_u; // CRC handled by ota_perform() / ignored here
        if (!ctx->is_ble && ctx->tcp_fd >= 0) {
            (void)ota_perform(ctx->tcp_fd, sz);   // reboots on success; handles replies
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
    CMD("settoken", true,  cmd_settoken),
    CMD("diag",     true,  cmd_diag),
    CMD("led_on",  true,  cmd_led_on),
    CMD("led_off", true,  cmd_led_off),
    CMD("version", false, cmd_version),
    CMD("ota",     true,  cmd_ota), 
    CMD("setwifi", true,  cmd_setwifi),
    CMD("errsrc",  false, cmd_errsrc),
};
const size_t CMD_COUNT = sizeof(CMDS) / sizeof(CMDS[0]);
