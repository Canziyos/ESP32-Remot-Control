idf_component_register(
  SRCS
    cmd.c
    cmd_bus.c
    cmd_reply.c
    cmd_table.c
    cmd_base.c
    cmd_auth.c
    cmd_wifi.c
    cmd_led.c
    cmd_diag.c
    cmd_ota.c
  INCLUDE_DIRS include
  REQUIRES nvs_flash app_update esp_system
)