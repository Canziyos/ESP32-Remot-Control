// standalone test of dht sensor.

// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"

// #include "driver/gpio.h"
// #include "esp_timer.h"
// #include "esp_log.h"

// #if __has_include("esp_rom_sys.h")
//   #include "esp_rom_sys.h"
//   #define dht_delay_us  esp_rom_delay_us
// #else
//   #include "rom/ets_sys.h"
//   #define dht_delay_us  ets_delay_us
// #endif

// static const char *TAG = "DHT_TEST";

// /* ========= CONFIG ========= */
// #define DHT_PIN        13      
// #define POLL_PERIOD_MS 2000    
// /* ========================= */

// /* Wait until pin reaches 'level' or timeout (us). */
// static bool wait_for_level(int gpio, int level, uint32_t timeout_us) {
//     int64_t start = esp_timer_get_time();
//     while (gpio_get_level(gpio) != level) {
//         if ((uint32_t)(esp_timer_get_time() - start) > timeout_us) return false;
//     }
//     return true;
// }

// /* Do a full DHT transaction; fill out[5] with 40 bits. */
// static bool dht_read_raw_bytes(int gpio, uint8_t out[5]) {
//     memset(out, 0, 5);

//     // Host start: pull low ~18ms, then release
//     gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
//     gpio_set_level(gpio, 1);
//     dht_delay_us(10);
//     gpio_set_level(gpio, 0);
//     dht_delay_us(18000);
//     gpio_set_level(gpio, 1);

//     // Sensor drives the line (external pull-up on common modules)
//     gpio_set_direction(gpio, GPIO_MODE_INPUT);

//     // Sensor response: ~80us low, then ~80us high
//     if (!wait_for_level(gpio, 0, 1000)) return false;
//     if (!wait_for_level(gpio, 1,  300)) return false;

//     // 40 data bits
//     for (int bit = 0; bit < 40; ++bit) {
//         if (!wait_for_level(gpio, 0, 100)) return false; // start of bit (low)
//         if (!wait_for_level(gpio, 1, 100)) return false; // rising edge
//         int64_t t0 = esp_timer_get_time();
//         if (!wait_for_level(gpio, 0, 200)) return false; // end of high pulse
//         int high_us = (int)(esp_timer_get_time() - t0);

//         int byte = bit / 8;
//         out[byte] <<= 1;
//         if (high_us > 50) out[byte] |= 1; // ~70us => '1', ~26us => '0'
//     }

//     // Checksum
//     uint8_t sum = (uint8_t)(out[0] + out[1] + out[2] + out[3]);
//     return sum == out[4];
// }

// /* Decode to °C / %RH. Auto-detect DHT11 vs DHT22-ish. */
// static bool dht_read(float *t_c, float *rh) {
//     if (!t_c || !rh) return false;

//     // make sure pulls are disabled (module usually has a 4.7k pull-up)
//     gpio_pullup_dis(DHT_PIN);
//     gpio_pulldown_dis(DHT_PIN);

//     uint8_t raw[5];
//     if (!dht_read_raw_bytes(DHT_PIN, raw)) return false;

//     if (raw[1] == 0 && raw[3] == 0) {
//         // Likely DHT11 (integers)
//         *rh  = (float)raw[0];
//         *t_c = (float)raw[2];
//     } else {
//         // DHT22/AM2302 (tenths)
//         uint16_t rh10 = ((uint16_t)raw[0] << 8) | raw[1];
//         int16_t  t10  = ((int16_t)raw[2] << 8) | raw[3];
//         if (t10 & 0x8000) t10 = -(t10 & 0x7FFF);
//         *rh  = (float)rh10 / 10.0f;
//         *t_c = (float)t10  / 10.0f;
//     }
//     return true;
// }

// void app_main(void) {
//     ESP_LOGI(TAG, "DHT quick test on GPIO %d", DHT_PIN);
//     // Idle high to avoid spurious start pulses
//     gpio_reset_pin(DHT_PIN);
//     gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
//     gpio_set_level(DHT_PIN, 1);

//     vTaskDelay(pdMS_TO_TICKS(1000)); // small settle

//     for (;;) {
//         float t = 0, h = 0;
//         bool ok = dht_read(&t, &h);

//         if (ok) {
//             ESP_LOGI(TAG, "T=%.1f °C  RH=%.1f %%", (double)t, (double)h);
//         } else {
//             ESP_LOGW(TAG, "read failed (check wiring, pull-up, and period)");
//         }

//         vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
//     }
// }
