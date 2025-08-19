#include <stdint.h>
#include "commands.h"
#include "ota_handler.h"

void cmd_ota(const char *args, cmd_ctx_t *ctx){
    if (!args||!*args){ reply(ctx,"BADFMT\n"); return; }
    unsigned size_u=0, crc_u=0;
    if (sscanf(args,"%u %x",&size_u,&crc_u)==2 && size_u>0){
        (void)crc_u; // handled internally if needed
        if (!ctx->is_ble && ctx->tcp_fd>=0) (void)ota_perform(ctx->tcp_fd,(uint32_t)size_u);
        else reply(ctx,"OTA_UNSUPPORTED\n");
    } else reply(ctx,"BADFMT\n");
}
