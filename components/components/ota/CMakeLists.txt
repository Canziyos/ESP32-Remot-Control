idf_component_register(
  SRCS
    ota_handler.c
    ota_session.c
    ota_writer.c
  INCLUDE_DIRS "include"      # public header (ota_handler.h)
  PRIV_INCLUDE_DIRS "priv"    # internal headers
  REQUIRES app_update esp_partition lwip freertos app_config
)
