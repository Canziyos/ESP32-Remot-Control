// gatt_server.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "alerts.h"

void gatt_server_init(void);

// TX reply (same text as TCP).
void gatt_server_send_status(const char *s);

// Wi-Fi status notify (can be a no-op if muted); edge-triggered inside gatt_server.c.
void gatt_server_notify_errsrc(const char *err);

// BLE alert push used by alerts.c (transport-agnostic callers use alert_raise()).
void gatt_alert_notify(const alert_record_t *rec);