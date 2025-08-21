# components/cmd/CMakeLists.txt
idf_component_register(
  SRCS
    command.c
    cmd_bus.c
    cmd_reply.c
    cmd_table.c
    cmd_base.c
    cmd_auth.c
    cmd_wifi.c
    cmd_led.c
    cmd_diag.c
    cmd_ota.c
    cmd_dht.c
  INCLUDE_DIRS
    "include"
  PRIV_REQUIRES
    syscoord         # syscoord_mark_tcp_authed(), syscoord_get_mode(), etc.
    net            # wifi_set_credentials() and Wi-Fi helpers
    ota              # ota_perform / ota_xport API
    errsrc           # errsrc_get(), errsrc_get_code()
    bootflag         # bootflag_is_post_rollback()
    nvs_flash        # NVS in cmd_auth
    app_update       # esp_ota_ops, esp_app_desc_t, etc.
    esp_partition    # partition info in cmd_diag
    app_config     # only if app_cfg.h lives in a header-only 'app' component
)
