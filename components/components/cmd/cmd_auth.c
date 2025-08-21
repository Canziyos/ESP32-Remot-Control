#include <string.h>
#include <stdbool.h>
#include "commands.h"
#include "command.h"
#include "app_cfg.h"
#include "nvs.h"
#include "syscoord.h"
#include "esp_err.h"

static char s_auth_token[65] = APP_DEFAULT_AUTH_TOKEN;
static bool s_auth_loaded = false;
#if defined(APP_DEFAULT_AUTH_TOKEN)
_Static_assert(sizeof(APP_DEFAULT_AUTH_TOKEN) <= 65, "APP_DEFAULT_AUTH_TOKEN too long");
#endif

static void auth_token_load(void){
    if (s_auth_loaded) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_AUTH, NVS_READONLY, &h) == ESP_OK){
        size_t len = sizeof(s_auth_token);
        (void)nvs_get_str(h, NVS_KEY_AUTH_TOKEN, s_auth_token, &len);
        nvs_close(h);
    }
    s_auth_loaded = true;
}

static inline void trim_trailing(char *s){
    if (!s) return;
    size_t n = strlen(s);
    while (n && (unsigned char)s[n-1] <= ' ') s[--n] = '\0';
}

static esp_err_t auth_token_save(const char *tok){
    nvs_handle_t h; esp_err_t err = nvs_open(NVS_NS_AUTH, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_AUTH_TOKEN, tok ? tok : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK){
        strncpy(s_auth_token, tok ? tok : "", sizeof(s_auth_token)-1);
        s_auth_token[sizeof(s_auth_token)-1] = '\0';
        s_auth_loaded = true;
    }
    return err;
}

void cmd_auth(const char *args, cmd_ctx_t *ctx){
    auth_token_load();
    if (!args) { cmd_reply(ctx, "DENIED\n"); return; }

    char buf[66];
    strncpy(buf, args, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    trim_trailing(buf);

    if (strcmp(buf, s_auth_token) == 0){
        ctx->authed = true;

        // Only TCP auth establishes the “control path OK”
        if (ctx->xport == CMD_XPORT_TCP) {
            syscoord_mark_tcp_authed();
            syscoord_control_path_ok("TCP");
        }
        cmd_reply(ctx, "OK\n");
    } else {
        cmd_reply(ctx, "DENIED\n");
    }
}

void cmd_settoken(const char *args, cmd_ctx_t *ctx){
    if (!ctx->authed){ cmd_reply(ctx, "DENIED\n"); return; }
    if (!args || !*args){ cmd_reply(ctx, "usage: SETTOKEN <newtoken>\n"); return; }

    char buf[65];
    strncpy(buf, args, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
    trim_trailing(buf);

    if (!*buf){ cmd_reply(ctx, "ERR empty token\n"); return; }
    if (strlen(buf) >= sizeof(s_auth_token)){ cmd_reply(ctx, "ERR token too long\n"); return; }

    cmd_reply(ctx, auth_token_save(buf) == ESP_OK ? "OK\n" : "ERR\n");
}
