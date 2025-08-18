idf_component_register(
  SRCS "ota_handler.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES core app_update
)