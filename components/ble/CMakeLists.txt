# components/ble/CMakeLists.txt
idf_component_register(
  SRCS
    ble_ids.c
    gatt/gatt_server.c
    gatt/gatt_attrs.c
    gatt/ble_cmd.c
    gatt/gatt_notify.c
    gatt/gatt_wifi_cred.c
    gatt/ble_ota.c
    fallback/fb_core.c
    fallback/fb_gap.c
    fallback/fb_worker.c
    priv/ota_bridge.c          
  INCLUDE_DIRS "include"       # public headers (visible to other components)
  PRIV_INCLUDE_DIRS "priv"     # private headers (only for this component)
  REQUIRES bt ota syscoord
)
