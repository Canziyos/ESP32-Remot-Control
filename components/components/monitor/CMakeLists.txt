idf_component_register(
  SRCS
    monitor.c
    monitor_policy.c
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "."
  PRIV_REQUIRES alerts bootflag errsrc app_update esp_partition
)
