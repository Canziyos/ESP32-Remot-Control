#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- System coordination modes ----------
 * WAIT_CONTROL : booted, waiting for a valid control path (rollback window active if needed).
 * NORMAL       : control path established (TCP + AUTH proven); BLE lifeboat hidden.
 * RECOVERY     : rollback didn’t restore control; BLE lifeboat enabled (final outpost).
 */
typedef enum {
    SC_MODE_STARTUP = 0,   // internal/transient
    SC_MODE_WAIT_CONTROL,
    SC_MODE_NORMAL,
    SC_MODE_RECOVERY
} sc_mode_t;

/* Init early (after nvs_flash_init). */
void syscoord_init(void);

/* Mark that TCP auth succeeded (called by commands.c on AUTH OK over TCP). */
void syscoord_mark_tcp_authed(void);

/* Promote to NORMAL only for TCP + after auth. (BLE must NOT call this.) */
void syscoord_control_path_ok(const char *source);

/* Event inputs the coordinator listens to. */
void syscoord_on_wifi_state(bool up);          // IP up/down
void syscoord_on_tcp_clients(int count);       // reserved for future policy
void syscoord_on_ble_state(bool connected);    // informational only

/* Manual escalation hook (rarely needed). */
void syscoord_on_no_control_path(void);

/* Read current coordination mode. */
sc_mode_t syscoord_get_mode(void);
void syscoord_on_ble_service_started(void);
#ifdef __cplusplus
}
#endif
