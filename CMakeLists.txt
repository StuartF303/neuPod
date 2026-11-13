cmake_minimum_required(VERSION 3.13)

# Select the target with -DNEUPOD_PLATFORM=rp2350|esp32s3|host|y2 (default rp2350).
# Use a separate build directory per platform, e.g. build/rp2350, build/host, build/y2.
set(NEUPOD_PLATFORM "rp2350" CACHE STRING "Target platform: rp2350, esp32s3, host or y2")
set_property(CACHE NEUPOD_PLATFORM PROPERTY STRINGS rp2350 esp32s3 host y2)

# The Y2 is cross-built; the toolchain must be known before project().
if(NEUPOD_PLATFORM STREQUAL "y2" AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain-y2.cmake)
endif()

# UI resolution profiles (docs/ui.md, ADR-0018). The product panel is 320x240; hosted builds (the host simulator and the
# Y2 testbed) also carry the 480x360 profile and its larger fonts, which the RP2350 / ESP32-S3 images must not pay flash
# for. ON by default for host and y2, OFF otherwise; override with -DNEUPOD_UI_HIRES=ON|OFF.
if(NEUPOD_PLATFORM STREQUAL "host" OR NEUPOD_PLATFORM STREQUAL "y2")
  set(NEUPOD_UI_HIRES_DEFAULT ON)
else()
  set(NEUPOD_UI_HIRES_DEFAULT OFF)
endif()
option(NEUPOD_UI_HIRES "Compile the 480x360 UI profile and its fonts (hosted builds)" ${NEUPOD_UI_HIRES_DEFAULT})
if(NEUPOD_UI_HIRES)
  add_compile_definitions(NEUPOD_UI_HIRES=1)
endif()
# Debug lines on screens (Home: the card summary "SDHC 32 GB"; the boot self-test list instead of the release splash's progress line): off in the product build.
option(NEUPOD_UI_DEBUG "Debug lines on the UI (Home card summary, debug splash)" OFF)
if(NEUPOD_UI_DEBUG)
  add_compile_definitions(NEUPOD_UI_DEBUG=1)
endif()
# Album art (docs/ui.md "Album art"): 160 x 160 thumbnails and a 24-album cache (1.2 MB) need RAM the Pico 2 W does not have (ADR-0001 amendment:
# frozen). ON for the hosted builds; an RP2350B board with PSRAM turns it on with -DNEUPOD_ART_HIRES=ON.
option(NEUPOD_ART_HIRES "160x160 album-art thumbnails with a 24-album cache (hosted builds, RP2350B with PSRAM)" ${NEUPOD_UI_HIRES_DEFAULT})
if(NEUPOD_ART_HIRES)
  add_compile_definitions(NEUPOD_ART_HIRES=1)
endif()

if(NEUPOD_PLATFORM STREQUAL "rp2350")
  # The Pico SDK must be imported before project() so it can set the toolchain.
  include(platforms/rp2350/sdk_import.cmake)
  project(neupod C CXX ASM)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_CXX_STANDARD 17)
  pico_sdk_init()
  add_compile_definitions(NEUPOD_RP2350=1)

  # FreeRTOS SMP (RP2350 port) must exist before app/, which links it. Also defines the SD-card
  # and cyw43 arch glue the runtime needs.
  add_subdirectory(platforms/rp2350/freertos)
  add_subdirectory(app)
  add_subdirectory(platforms/rp2350)

elseif(NEUPOD_PLATFORM STREQUAL "host")
  project(neupod C)
  set(CMAKE_C_STANDARD 11)
  find_package(Threads REQUIRED)
  enable_testing()

  add_subdirectory(hal/host/freertos) # FreeRTOS POSIX port, before app/ which links it
  add_subdirectory(app)
  add_subdirectory(hal/host)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
  endif()

elseif(NEUPOD_PLATFORM STREQUAL "y2")
  # Innioasis Y2 testbed: the host build's FreeRTOS POSIX port and portable app, with the Y2 HALs (docs/y2.md).
  project(neupod C)
  set(CMAKE_C_STANDARD 11)
  find_package(Threads REQUIRED)
  add_compile_definitions(NEUPOD_PLATFORM_Y2=1)

  add_subdirectory(hal/host/freertos) # FreeRTOS POSIX port, before app/ which links it
  add_subdirectory(app)
  add_subdirectory(platforms/y2)

elseif(NEUPOD_PLATFORM STREQUAL "esp32s3")
  # ESP-IDF owns the CMake project for its targets and cannot be nested under another
  # project(); its project lives in platforms/esp32s3 and pulls in the same app/ sources.
  message(FATAL_ERROR
    "esp32s3 is built by ESP-IDF directly:\n"
    "  . ~/esp/esp-idf/export.sh && idf.py -C platforms/esp32s3 -B build/esp32s3 build\n"
    "or: scripts/build.sh esp32s3")

else()
  message(FATAL_ERROR "Unknown NEUPOD_PLATFORM '${NEUPOD_PLATFORM}' (rp2350, esp32s3, host, y2)")
endif()
