#include <string.h>
#include "commands.h"
#include "syscoord.h"
#include "errsrc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char* mode_to_str(sc_mode_t m){
    switch(m){
        case SC_MODE_WAIT_CONTROL: return "WAIT_CONTROL";
        case SC_MODE_NORMAL:       return "NORMAL";
        case SC_MODE_RECOVERY:     return "RECOVERY";
        default:                   return "STARTUP";
    }
}

void cmd_ping(const char *args, cmd_ctx_t *ctx){ (void)args; cmd_reply(ctx,"PONG\n"); }

void cmd_version(const char *args, cmd_ctx_t *ctx){
    (void)args;
    esp_app_desc_t d = (esp_app_desc_t){0};
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && esp_ota_get_partition_description(run,&d)==ESP_OK && d.version[0]) {
        cmd_replyf(ctx,"%s\n", d.version); return;
    }
#ifdef CONFIG_APP_PROJECT_VER
    cmd_replyf(ctx,"%s\n", CONFIG_APP_PROJECT_VER);
#else
    cmd_reply(ctx,"unknown\n");
#endif
}

void cmd_errsrc(const char *args, cmd_ctx_t *ctx){
    (void)args;
    const char *err = errsrc_get(); if(!err) err="NONE";
    errsrc_t code = errsrc_get_code();
    sc_mode_t m = syscoord_get_mode();
    cmd_replyf(ctx,"mode=%s errsrc=%u %s\n", mode_to_str(m),(unsigned)code,err);
}
