// components/dht/dht.c
#include <string.h>
#include "dht.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#if __has_include("esp_rom_sys.h")
  #include "esp_rom_sys.h"
  #define dht_delay_us  esp_rom_delay_us
#else
  #include "rom/ets_sys.h"
  #define dht_delay_us  ets_delay_us
#endif

static const char *TAG = "DHT";

/* ---------- Internal state ---------- */
typedef struct {
    bool inited;
    int gpio;
    uint32_t def_period_ms;

    volatile bool stream_on;
    volatile uint32_t period_ms;

    volatile TickType_t last_tick;
    volatile dht_sample_t last;

    TaskHandle_t task;
} dht_state_t;

static dht_state_t S = {0};
/* Protects concurrent access to S.last / S.last_tick. */
static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---------- Low-level timing helpers ---------- */
static bool wait_for_level(int gpio, int level, uint32_t timeout_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level) {
        if ((uint32_t)(esp_timer_get_time() - start) > timeout_us) return false;
    }
    return true;
}

static bool dht_read_raw_bytes(int gpio, uint8_t out[5]) {
    memset(out, 0, 5);

    // --- DRIVE START PULSE ---
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 1);
    dht_delay_us(10);
    gpio_set_level(gpio, 0);
    dht_delay_us(18000); // ~18 ms

    // Release line and switch to input to let sensor drive it
    gpio_set_level(gpio, 1);                     // bring it high
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    // Reply: ~80us low, then ~80us high
    if (!wait_for_level(gpio, 0, 1000)) return false;
    if (!wait_for_level(gpio, 1,  200)) return false;

    // 40 bits...
    for (int bit = 0; bit < 40; ++bit) {
        if (!wait_for_level(gpio, 0, 100)) return false;
        if (!wait_for_level(gpio, 1, 100)) return false;
        int64_t t0 = esp_timer_get_time();
        if (!wait_for_level(gpio, 0, 200)) return false;
        int high_us = (int)(esp_timer_get_time() - t0);

        int byte = bit / 8;
        out[byte] <<= 1;
        if (high_us > 50) out[byte] |= 1; // ~70us => 1, ~26us => 0
    }

    uint8_t sum = (uint8_t)(out[0] + out[1] + out[2] + out[3]);
    return (sum == out[4]);
}


/* Read one sample and convert to °C / %RH. Supports DHT11 & DHT22. */
static bool dht_hw_read(float *out_t_c, float *out_rh) {
    if (!out_t_c || !out_rh) return false;
    int gpio = S.gpio;

    gpio_pullup_dis(gpio);    // module has its own pull-up
    gpio_pulldown_dis(gpio);

    uint8_t raw[5];
    if (!dht_read_raw_bytes(gpio, raw)) return false;

    // Heuristic: DHT11 often has raw[1]==0 && raw[3]==0 (integer only)
    if (raw[1] == 0 && raw[3] == 0) {
        *out_rh  = (float)raw[0];
        *out_t_c = (float)raw[2];
    } else {
        uint16_t rh10 = ((uint16_t)raw[0] << 8) | raw[1];
        int16_t  t10  = ((int16_t)raw[2] << 8) | raw[3];
        if (t10 & 0x8000) t10 = -(t10 & 0x7FFF);
        *out_rh  = (float)rh10 / 10.0f;
        *out_t_c = (float)t10  / 10.0f;
    }
    return true;
}

/* ---------- Sampler task ---------- */
static void dht_sampler_task(void *pv) {
    (void)pv;
    for (;;) {
        if (!S.stream_on) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        float t = 0.0f, h = 0.0f;
        bool ok = dht_hw_read(&t, &h);
        TickType_t now = xTaskGetTickCount();

        portENTER_CRITICAL(&s_dht_mux);
        S.last.valid  = ok;
        S.last.temp_c = ok ? t : 0.0f;
        S.last.rh     = ok ? h : 0.0f;
        S.last.age_ms = 0;
        S.last_tick   = now;
        portEXIT_CRITICAL(&s_dht_mux);

        uint32_t ms = S.period_ms ? S.period_ms : S.def_period_ms;
        if (ms < 200) ms = 200; // be gentle
        ulTaskNotifyTake(pdTRUE, ms / portTICK_PERIOD_MS);
    }
}

/* ---------- Public API ---------- */
esp_err_t dht_init(const dht_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;

    S.gpio          = cfg->gpio;
    S.def_period_ms = cfg->period_ms ? cfg->period_ms : 2000;
    S.period_ms     = S.def_period_ms;
    S.stream_on     = false;

    portENTER_CRITICAL(&s_dht_mux);
    S.last = (dht_sample_t){ .valid=false, .temp_c=0, .rh=0, .age_ms=0 };
    S.last_tick = xTaskGetTickCount();
    portEXIT_CRITICAL(&s_dht_mux);

    S.inited = true;
    return ESP_OK;
}

esp_err_t dht_start(void) {
    if (!S.inited) return ESP_ERR_INVALID_STATE;
    if (S.task)    return ESP_OK;
    if (xTaskCreate(dht_sampler_task, "dht.sampler", 3072, NULL, 4, &S.task) != pdPASS)
        return ESP_FAIL;
    return ESP_OK;
}

void dht_get_stream_state(bool *on, uint32_t *every_ms) {
    if (on)       *on = S.stream_on;
    if (every_ms) *every_ms = S.period_ms;
}

void dht_read_latest(dht_sample_t *out) {
    if (!out) return;
    TickType_t last_tick;
    portENTER_CRITICAL(&s_dht_mux);
    *out = S.last;
    last_tick = S.last_tick;
    portEXIT_CRITICAL(&s_dht_mux);
    TickType_t now = xTaskGetTickCount();
    out->age_ms = (uint32_t)((now - last_tick) * portTICK_PERIOD_MS);
}

void dht_set_stream(bool on, uint32_t every_ms) {
    S.stream_on = on;
    if (every_ms) S.period_ms = every_ms;
    if (S.task) xTaskNotifyGive(S.task);
    ESP_LOGI(TAG, "stream=%d interval=%u ms", on ? 1 : 0,
             (unsigned)(every_ms ? every_ms : S.period_ms));
}
