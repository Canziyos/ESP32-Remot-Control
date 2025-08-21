#include "command_bus.h"
#include "commands.h"
#include "esp_log.h"

static const char *TAG = "DHT_TASK";

static void dht_cmd_task(void *pv) {
    cmd_t cmd;
    while (cmd_bus_receive(&cmd, portMAX_DELAY)) {
        switch (cmd) {
        case CMD_DHT_QUERY: {
            dht_sample_t s;
            dht_read_latest(&s);
            if (!s.valid) {
                // No valid sample yet
                // For now, just log — reply path TBD (ctx needed).
                ESP_LOGI(TAG, "DHT_QUERY: no valid sample");
            } else {
                ESP_LOGI(TAG, "DHT_QUERY: T=%.1fC RH=%.1f%% age=%ums",
                         s.temp_c, s.rh, (unsigned)s.age_ms);
            }
            break;
        }
        case CMD_DHT_STREAM_ON:
            dht_set_stream(true, 0);
            ESP_LOGI(TAG, "DHT_STREAM_ON");
            break;

        case CMD_DHT_STREAM_OFF:
            dht_set_stream(false, 0);
            ESP_LOGI(TAG, "DHT_STREAM_OFF");
            break;

        default:
            // ignore other commands
            break;
        }
    }
}

void dht_task_start(void) {
    configASSERT(cmd_bus_is_ready());
    if (!cmd_bus_is_ready()) return;
    xTaskCreate(dht_cmd_task, "dht.cmd", 3072, NULL, 4, NULL);
}
