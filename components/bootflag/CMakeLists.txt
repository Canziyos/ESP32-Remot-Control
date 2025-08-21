# components/bootflag/CMakeLists.txt
idf_component_register(
  SRCS "bootflag.c"
  INCLUDE_DIRS "include"
  REQUIRES nvs_flash
)
