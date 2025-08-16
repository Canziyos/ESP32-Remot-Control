#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALERT_NONE = 0,
    ALERT_OTA_APPLY_FAIL,
    ALERT_OTA_VERIFY_FAIL,
    ALERT_ROLLBACK_EXECUTED,
    ALERT_WATCHDOG_RESET,
    ALERT_FLASH_WRITE_ERROR,
    ALERT_FS_MOUNT_FAIL,
    ALERT_IMAGE_INVALID,
    ALERT_TCP_FATAL,
    ALERT_BLE_FATAL,
} alert_code_t;

typedef struct {
    uint16_t     seq;     // monotonically increasing sequence
    alert_code_t code;    // what happened
    char         detail[80]; // short human-readable note
} alert_record_t;

typedef void (*alerts_cb_t)(const alert_record_t *rec);

// Raise a new alert (edge-triggered by seq increment).
void alert_raise(alert_code_t code, const char *detail_opt);

// Read the latest alert snapshot (returns seq=0 & code=ALERT_NONE if none yet).
void alert_latest(alert_record_t *out);

// Set/replace the single subscriber that will be notified on every alert_raise().
void alerts_subscribe(alerts_cb_t cb);

#ifdef __cplusplus
}
#endif
