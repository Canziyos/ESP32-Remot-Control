idf_component_register(
  SRCS "command.c" "commands.c" "command_bus.c"
  INCLUDE_DIRS "."
  REQUIRES core wifi ota
)
