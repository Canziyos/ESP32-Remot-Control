idf_component_register(
  SRCS
    sys_core.c
    sys_events.c
    sys_policy.c
    sys_sink.c
  INCLUDE_DIRS include
  REQUIRES alerts bootflag errsrc monitor ble
)
