#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Spawn a task to serve one accepted TCP client fd */
void tcp_client_spawn(int fd);

/* Listener keeps the client count; client calls these on connect/disconnect */
void tcp_on_client_connected(void);
void tcp_on_client_disconnected(void);

#ifdef __cplusplus
}
#endif
