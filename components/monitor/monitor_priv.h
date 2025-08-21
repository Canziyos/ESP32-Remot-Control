#pragma once
#include <stdbool.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

// Return true if current running image is an OTA slot and in a rollback-eligible state.
bool monitor_is_rollback_eligible(const esp_partition_t *run, esp_ota_img_states_t *out_state);

// Try to rollback now (official API first, then manual fallback).
// On success this reboots (no return). On failure returns false and caller continues.
bool monitor_try_rollback_now(const char *why);
