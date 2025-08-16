// components/core/include/monitor.h
#pragma once
#include <stdint.h>
#include "errsrc.h"  // for errsrc_t.

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*monitor_timeout_cb_t)(void);

void monitor_set_timeout_cb(monitor_timeout_cb_t cb);
void health_monitor_start(uint32_t ms_window);           // Now just resets state. No timer window.
void health_monitor_control_ok(const char *path);        // Clears escalation and streak.

/* event-driven escalation. Call with ERRSRC_* codes, or ERRSRC_NONE to reset. */
void monitor_on_wifi_error(errsrc_t e);

#ifdef __cplusplus
}
#endif
