idf_component_register(
  SRCS "ble_fallback.c" "gatt_server.c"
  INCLUDE_DIRS "."
  REQUIRES core command
  PRIV_REQUIRES bt
)
