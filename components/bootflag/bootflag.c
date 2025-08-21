// components/bootflag.c
#include <stdint.h>
#include "bootflag.h"
#include "nvs.h"

#define BF_NS  "syscoord"
#define BF_KEY "post_rb"

static void write_u8(uint8_t v){
    nvs_handle_t h; if (nvs_open(BF_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, BF_KEY, v); nvs_commit(h); nvs_close(h);
}
static bool read_u8(uint8_t *out){
    nvs_handle_t h; if (nvs_open(BF_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_u8(h, BF_KEY, out); nvs_close(h); return e==ESP_OK;
}

void bootflag_set_post_rollback(bool on) {
    uint8_t cur = 0;
    if (read_u8(&cur) && cur == (on ? 1 : 0)) return; // no-op.
    write_u8(on ? 1 : 0);
}

bool bootflag_is_post_rollback(void) {
    uint8_t v = 0;
    return read_u8(&v) && v != 0;
}
