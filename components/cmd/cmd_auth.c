#include <string.h>
#include <stdbool.h>
#include "commands.h"
#include "app_cfg.h"
#include "nvs.h"
#include "syscoord.h"

static char s_auth_token[65] = APP_DEFAULT_AUTH_TOKEN;
static bool s_auth_loaded = false;

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

static esp_err_t auth_token_save(const char *tok){
    nvs_handle_t h; esp_err_t err = nvs_open(NVS_NS_AUTH,NVS_READWRITE,&h);
    if (err!=ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_AUTH_TOKEN, tok?tok:"");
    if (err==ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err==ESP_OK){
        strncpy(s_auth_token, tok?tok:"", sizeof(s_auth_token)-1);
        s_auth_token[sizeof(s_auth_token)-1] = '\0';
        s_auth_loaded = true;
    }
    return err;
}

void cmd_auth(const char *args, cmd_ctx_t *ctx){
    auth_token_load();
    if (args && strcmp(args, s_auth_token)==0){
        ctx->authed = true;
        if (!ctx->is_ble){ syscoord_mark_tcp_authed(); syscoord_control_path_ok("TCP"); }
        reply(ctx,"OK\n");
    }else reply(ctx,"DENIED\n");
}

void cmd_settoken(const char *args, cmd_ctx_t *ctx){
    if (!ctx->authed){ reply(ctx,"DENIED\n"); return; }
    if (!args||!*args){ reply(ctx,"usage: SETTOKEN <newtoken>\n"); return; }
    if (strlen(args) >= sizeof(s_auth_token)){ reply(ctx,"ERR token too long\n"); return; }
    reply(ctx, auth_token_save(args)==ESP_OK ? "OK\n" : "ERR\n");
}
