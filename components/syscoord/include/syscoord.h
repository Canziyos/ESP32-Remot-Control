#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SC_MODE_STARTUP = 0,
    SC_MODE_WAIT_CONTROL,
    SC_MODE_NORMAL,
    SC_MODE_RECOVERY
} sc_mode_t;

void syscoord_init(void);

void syscoord_mark_tcp_authed(void);
void syscoord_control_path_ok(const char *source);

void syscoord_on_wifi_state(bool up);
void syscoord_on_tcp_clients(int count);
void syscoord_on_ble_state(bool connected);

void syscoord_on_ble_service_started(void);   // called by GATT once services are up
void syscoord_on_no_control_path(void);

sc_mode_t syscoord_get_mode(void);

#ifdef __cplusplus
}
#endif
