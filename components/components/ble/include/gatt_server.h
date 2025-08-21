// include/gatt_server.h
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Initialize GATT services/characteristics and register callbacks. */
void gatt_server_init(void);

/* Send a single status/telemetry line over the TX characteristic. */
void gatt_server_send_status(const char *line);

#ifdef __cplusplus
}
#endif
