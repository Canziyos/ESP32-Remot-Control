# components/net/CMakeLists.txt

set(srcs
  wifi/wifi_api.c
  wifi/wifi_backoff.c
  wifi/wifi_event.c
  tcp/tcp_listener.c
  tcp/tcp_client.c
)

idf_component_register(
  SRCS ${srcs}
  # Public headers for 'net' live here:
  INCLUDE_DIRS "include"
  PRIV_REQUIRES
    errsrc        # errsrc_set/errsrc_get, codes
    monitor       # monitor_on_wifi_error, etc.
    syscoord      # syscoord_on_wifi_state
    cmd           # tcp dispatch / command write path
    nvs_flash     # Wi-Fi creds
    esp_wifi
    esp_netif
    esp_event
    lwip          # sockets
)
