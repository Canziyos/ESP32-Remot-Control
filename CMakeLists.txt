cmake_minimum_required(VERSION 3.16)

# Use your custom partitions.csv in a supported way:
set(PARTITION_TABLE_CUSTOM 1)
set(PARTITION_TABLE_CUSTOM_FILENAME partitions.csv)

# scan nested component roots:
set(EXTRA_COMPONENT_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/components/net
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# trims unused components based on REQUIRES/PRIV_REQUIRES
idf_build_set_property(MINIMAL_BUILD ON)

project(lopy4)
