// main/main.c
#include "sdkconfig.h" 
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "syscoord.h"
#include "command_bus.h"
#include "wifi.h"
#include "bootflag.h"
#include "app_cfg.h"
#include "cmd_router.h"
#include "led.h"
#include "dht.h"

void app_main(void) {
    // sdkconfig's global verbosity (Menuconfig => Log output => Default log verbosity).
    esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);

    // One-time NVS init (with erase-on-upgrade fallback).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    // If we booted the factory image, clear any stale post-rollback latch.
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && run->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        bootflag_set_post_rollback(false);
    }

    syscoord_init();  // System coordination.
    // Command routing.
    cmd_bus_init();
   
    // DHT sampler.
    dht_cfg_t dcfg = {.gpio = DHT_GPIO, .period_ms = DHT_PERIOD_MS};
    ESP_ERROR_CHECK(dht_init(&dcfg));
    ESP_ERROR_CHECK(dht_start()); 

    // LED driver init (no task needed for simple on/off).
    ESP_ERROR_CHECK(led_init());
    cmd_router_start();

    // Use saved credentials from NVS.
    // wifi_start("ssid", "password");
    wifi_start(NULL, NULL);
}
