#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- System coordination modes ----------
 * WAIT_CONTROL : booted, health window active, waiting for a valid control path.
 * NORMAL       : control path established (typically TCP auth); BLE lifeboat hidden.
 * RECOVERY     : rollback didn’t restore control; BLE lifeboat enabled (final outpost).
 */
typedef enum {
    SC_MODE_STARTUP = 0,   // internal/transient; usually not observed by callers
    SC_MODE_WAIT_CONTROL,
    SC_MODE_NORMAL,
    SC_MODE_RECOVERY
} sc_mode_t;

/* Initialize the coordinator (call early in app_main, after nvs_flash_init()). */
void syscoord_init(void);

/* Report that a valid control path has been established (e.g., after TCP AUTH).
 * NOTE: By design, BLE should NOT call this (rollback-first policy). */
void syscoord_control_path_ok(const char *source);

/* Event inputs the coordinator listens to */
void syscoord_on_wifi_state(bool up);    // IP up/down
void syscoord_on_tcp_clients(int count); // authed TCP clients (reserved for future policy)
void syscoord_on_ble_state(bool connected); // BLE link presence (informational)

/* Manual/escalation hook to force RECOVERY (rarely needed). */
void syscoord_on_no_control_path(void);

/* Read current coordination mode (use in commands.c to gate risky commands). */
sc_mode_t syscoord_get_mode(void);

void syscoord_mark_tcp_authed(void);

#ifdef __cplusplus
}
#endif
