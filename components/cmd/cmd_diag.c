#include <stdbool.h>
#include "commands.h"
#include "syscoord.h"
#include "bootflag.h"
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
static const char* _ota_state_str(esp_ota_img_states_t st){
    switch(st){
        case ESP_OTA_IMG_NEW: return "NEW";
        case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
        case ESP_OTA_IMG_VALID: return "VALID";
        case ESP_OTA_IMG_INVALID: return "INVALID";
        case ESP_OTA_IMG_ABORTED: return "ABORTED";
        case ESP_OTA_IMG_UNDEFINED: default: return "UNDEFINED";
    }
}

void cmd_diag(const char *args, cmd_ctx_t *ctx){
    (void)args;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run) (void)esp_ota_get_state_partition(run, &st);

    sc_mode_t mode = syscoord_get_mode();
    bool post_rb = bootflag_is_post_rollback();

    replyf(ctx,
        "PART=%s SUB=%d OFF=0x%06x SIZE=%u\n"
        "OTA_STATE=%s\n"
        "POST_RB=%d\n"
        "MODE=%s\n",
        run ? run->label : "?", run ? run->subtype : -1,
        run ? (unsigned)run->address : 0, run ? (unsigned)run->size : 0,
        _ota_state_str(st), post_rb ? 1 : 0, mode_to_str(mode));
}
