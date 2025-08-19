idf_component_register(
  SRCS "ble_fallback.c" "gatt_server.c" "ble_cmd.c" "ble_ota_stub.c" "ble_ota.c"
  INCLUDE_DIRS include
  REQUIRES core cmd common
  PRIV_REQUIRES bt
)
