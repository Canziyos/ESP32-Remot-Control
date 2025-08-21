#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "wifi.h"

void cmd_setwifi(const char *args, cmd_ctx_t *ctx){
    if (!args || !*args) { cmd_reply(ctx, "BADFMT\n"); return; }
    char ssid[33] = {0}, pwd[65] = {0};
    if (sscanf(args, "%32s %64s", ssid, pwd) == 2) {
        wifi_set_credentials(ssid, pwd);
        cmd_reply(ctx, "WIFI_UPDATED\n");
    } else {
        cmd_reply(ctx, "BADFMT\n");
    }
}
