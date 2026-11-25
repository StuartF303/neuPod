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
option(NEUPOD_UI_HIRES "Compile the 480x360 and 600x450 UI profiles and their fonts (hosted builds)" ${NEUPOD_UI_HIRES_DEFAULT})
if(NEUPOD_UI_HIRES)
  add_compile_definitions(NEUPOD_UI_HIRES=1)
endif()
# The one profile a product (non-hosted) build compiles in as a constant: 320x240 (Pico 2 W, the default) or 600x450 (the Waveshare touch
# AMOLED 2.41 boards). Hosted builds carry every profile and pick one at run time from the panel size, so they ignore this.
if(NEUPOD_BOARD STREQUAL "amoled241")
  set(NEUPOD_UI_PROFILE_DEFAULT "600x450") # the Waveshare board's panel (NEUPOD_BOARD comes from the command line: scripts/build.sh rp2350-amoled241)
else()
  set(NEUPOD_UI_PROFILE_DEFAULT "320x240")
endif()
set(NEUPOD_UI_PROFILE "${NEUPOD_UI_PROFILE_DEFAULT}" CACHE STRING "UI profile of a product build: 320x240 or 600x450")
set_property(CACHE NEUPOD_UI_PROFILE PROPERTY STRINGS 320x240 600x450)
if(NOT NEUPOD_UI_PROFILE STREQUAL "320x240" AND NOT NEUPOD_UI_PROFILE STREQUAL "600x450")
  message(FATAL_ERROR "NEUPOD_UI_PROFILE must be 320x240 or 600x450 (got '${NEUPOD_UI_PROFILE}')")
endif()
if(NOT NEUPOD_UI_HIRES AND NEUPOD_UI_PROFILE STREQUAL "600x450")
  add_compile_definitions(NEUPOD_UI_PROFILE_600=1)
endif()
# Debug lines on screens (Home: the card summary "SDHC 32 GB"; the boot self-test list instead of the release splash's progress line): off in the product build.
option(NEUPOD_UI_DEBUG "Debug lines on the UI (Home card summary, debug splash)" OFF)
if(NEUPOD_UI_DEBUG)
  add_compile_definitions(NEUPOD_UI_DEBUG=1)
endif()
# The big-RAM player (docs/ui.md "Album art", UI-25): 160 x 160 art thumbnails with a 24-album cache (1.2 MB), browse art and Cover Flow, and
# the playlists, play counts and 128 ratings that share the option. The Pico 2 W does not have the RAM (ADR-0001 amendment: frozen). ON for the
# hosted builds and for the boards with PSRAM: the RP2350 AMOLED board here (its big buffers go to PSRAM, app/include/psram_bss.h) and the
# ESP32-S3 in platforms/esp32s3/CMakeLists.txt. -DNEUPOD_ART_HIRES=OFF gives the small player for comparison.
if(NEUPOD_UI_HIRES_DEFAULT OR NEUPOD_BOARD STREQUAL "amoled241")
  set(NEUPOD_ART_HIRES_DEFAULT ON)
else()
  set(NEUPOD_ART_HIRES_DEFAULT OFF)
endif()
option(NEUPOD_ART_HIRES "160x160 album-art thumbnails with a 24-album cache, Cover Flow, playlists (hosted builds, PSRAM boards)" ${NEUPOD_ART_HIRES_DEFAULT})
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
  if(NEUPOD_BOARD STREQUAL "amoled241")
    add_compile_definitions(NEUPOD_BOARD_AMOLED241=1) # Waveshare RP2350-Touch-AMOLED-2.41: no radio, no wheel, touch + 600x450 panel
    # Its 8 MB PSRAM holds the big buffers (app/include/psram_bss.h, platforms/rp2350/boards/amoled241/psram_amoled.c).
    add_compile_definitions(NEUPOD_PSRAM_SECTION=1)
    option(NEUPOD_PSRAM_FORCE_FAIL "amoled241 debug aid: report the PSRAM as failed, to test the boot's failure path" OFF)
    if(NEUPOD_PSRAM_FORCE_FAIL)
      add_compile_definitions(NEUPOD_PSRAM_FORCE_FAIL=1)
    endif()
  else()
    # The Pico 2 W bench rig is frozen with about 100 B of static RAM spare and can neither sleep nor switch itself off: power management
    # (the sleep setting, the idle policy, the sleep sequence) is compiled out (docs/adr/0021-power-sleep.md).
    add_compile_definitions(NEUPOD_POWER_MGMT=0)
  endif()

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
