cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(sensors_testing)

#spiffs_create_partition_image(storage storage_dir FLASH_IN_PROJECT)