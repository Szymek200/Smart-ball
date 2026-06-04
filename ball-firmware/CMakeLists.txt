# The master CMakeLists.txt for your firmware project
cmake_minimum_required(VERSION 3.16)

# Register the native example folder from your active .espressif directory as an external component
set(EXTRA_COMPONENT_DIRS 
    "/Users/deuter/.espressif/v6.0.1/esp-idf/examples/peripherals/uart/nmea0183_parser/main"
)





# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ball-firmware)

