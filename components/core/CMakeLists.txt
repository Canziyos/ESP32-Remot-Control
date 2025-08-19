idf_component_register(
  SRCS "syscoord.c" "monitor.c" "alerts.c" "bootflag.c" "errsrc.c"
  INCLUDE_DIRS include
  PRIV_REQUIRES ble nvs_flash app_update
)