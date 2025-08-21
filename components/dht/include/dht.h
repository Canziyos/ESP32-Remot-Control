#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;        // DHT data pin
    uint32_t period_ms;   // poll interval (ms)
} dht_cfg_t;

typedef struct {
    bool valid;       // last sample valid?
    float temp_c;      // temperature in °C
    float rh;          // relative humidity in %
    uint32_t age_ms;      // ms since it was captured
} dht_sample_t;

// Init with pin/period (does not start any task)
esp_err_t dht_init(const dht_cfg_t *cfg);

// Start background task (safe to call once)
esp_err_t dht_start(void);

// Copy latest sample (non-blocking).
void dht_read_latest(dht_sample_t *out);

// Enable/disable streaming (no-op for now; will be used by BLE/CMD later)
void dht_set_stream(bool on, uint32_t every_ms);

#ifdef __cplusplus
}
#endif
