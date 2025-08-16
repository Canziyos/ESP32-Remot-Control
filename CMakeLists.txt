cmake_minimum_required(VERSION 3.16)

# Use your custom partitions.csv in a supported way:
set(PARTITION_TABLE_CUSTOM 1)
set(PARTITION_TABLE_CUSTOM_FILENAME partitions.csv)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Optional: trims unused components based on REQUIRES/PRIV_REQUIRES
idf_build_set_property(MINIMAL_BUILD ON)

project(lopy4)
