// main/main.c
#include "nvs_flash.h"
#include "esp_log.h"
#include "syscoord.h"
#include "command_bus.h"
#include "led.h"
#include "wifi.h"


void app_main(void) {
    // One-time NVS init (with erase-on-upgrade fallback)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    syscoord_init();
    cmd_bus_init();
    led_task_start();

    // Primary path; BLE is owned by syscoord and will be started only in RECOVERY.
    wifi_start("my_password", "my_something");
}
