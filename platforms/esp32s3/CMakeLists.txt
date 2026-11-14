# ESP-IDF project: the full neuPod runtime on the Waveshare ESP32-S3-Touch-AMOLED-2.41 dev board (docs/esp32s3.md).
#   . ~/esp/esp-idf/export.sh
#   idf.py -C platforms/esp32s3 -B build/esp32s3 set-target esp32s3 build
# or: scripts/build.sh esp32s3
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)

# ESP-IDF ships its own FatFs (component "fatfs": ff.h, diskio.h, disk_* and f_* symbols). neuPod brings its own copy and
# configuration (app/third_party/fatfs, ffconf.h) over an SD-SPI diskio, exactly as the Y2 and host builds do, so the IDF one
# is kept out of the build entirely: no second ff.h on any include path, no duplicate f_* symbols at link time.
set(EXCLUDE_COMPONENTS fatfs)

# UI profile (docs/ui.md): this board's panel is 600x450. Override with -DNEUPOD_UI_PROFILE=320x240 (idf.py build -D...).
set(NEUPOD_UI_PROFILE "600x450" CACHE STRING "UI profile: 320x240 or 600x450")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(neupod)
