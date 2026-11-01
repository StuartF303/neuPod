# ESP-IDF project for the ESP32-S3 comparison build.
#   . ~/esp/esp-idf/export.sh
#   idf.py -C platforms/esp32s3 -B build/esp32s3 set-target esp32s3 build
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(neupod)
