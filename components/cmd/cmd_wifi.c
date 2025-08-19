#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "wifi.h"

void cmd_setwifi(const char *args, cmd_ctx_t *ctx){
    if (!args||!*args){ reply(ctx,"BADFMT\n"); return; }
    char ssid[33]={0}, pwd[65]={0};
    if (sscanf(args, "%32s %64s", ssid, pwd)==2){
        wifi_set_credentials(ssid, pwd);
        reply(ctx,"WIFI_UPDATED\n");
    } else reply(ctx,"BADFMT\n");
}
