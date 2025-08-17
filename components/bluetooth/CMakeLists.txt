idf_component_register(
  SRCS "ble_fallback.c" "gatt_server.c" "ble_cmd.c"
  INCLUDE_DIRS "."
  REQUIRES core command
  PRIV_REQUIRES bt
)
