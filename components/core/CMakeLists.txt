idf_component_register(
  SRCS "syscoord.c" "monitor.c" "alerts.c" "bootflag.c" "errsrc.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES bluetooth nvs_flash app_update
)