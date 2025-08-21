idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES
    syscoord        # components/syscoord.
    wifi            # components/net/wifi.
    cmd             # components/cmd.
    led             # components/led.
    bootflag        # components/bootflag.
  PRIV_REQUIRES
    nvs_flash
    app_update
)
