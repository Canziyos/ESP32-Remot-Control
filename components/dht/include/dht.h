#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio;            // DHT data pin.
    uint32_t period_ms;  // Default poll interval (ms).
} dht_cfg_t;

typedef struct {
    bool valid;      // Last sample valid?
    float temp_c;     // Temperature in °C.
    float rh;         // Relative humidity in %.
    uint32_t age_ms;  // ms since it was captured.
} dht_sample_t;

// Init with pin/period (does not start the sampler task).
esp_err_t dht_init(const dht_cfg_t *cfg);

// Start background sampler task (safe to call once).
esp_err_t dht_start(void);

// Copy latest sample (non-blocking).
void dht_read_latest(dht_sample_t *out);

// Enable/disable periodic sampling; if every_ms == 0, current/default period.
void dht_set_stream(bool on, uint32_t every_ms);

// Query current streaming state and period (returns via out params; either may be NULL).
void dht_get_stream_state(bool *on, uint32_t *every_ms);

#ifdef __cplusplus
}
#endif
