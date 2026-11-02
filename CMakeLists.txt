cmake_minimum_required(VERSION 3.13)

# Select the target with -DNEUPOD_PLATFORM=rp2350|esp32s3|host (default rp2350).
# Use a separate build directory per platform, e.g. build/rp2350, build/host.
set(NEUPOD_PLATFORM "rp2350" CACHE STRING "Target platform: rp2350, esp32s3 or host")
set_property(CACHE NEUPOD_PLATFORM PROPERTY STRINGS rp2350 esp32s3 host)

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

elseif(NEUPOD_PLATFORM STREQUAL "esp32s3")
  # ESP-IDF owns the CMake project for its targets and cannot be nested under another
  # project(); its project lives in platforms/esp32s3 and pulls in the same app/ sources.
  message(FATAL_ERROR
    "esp32s3 is built by ESP-IDF directly:\n"
    "  . ~/esp/esp-idf/export.sh && idf.py -C platforms/esp32s3 -B build/esp32s3 build\n"
    "or: scripts/build.sh esp32s3")

else()
  message(FATAL_ERROR "Unknown NEUPOD_PLATFORM '${NEUPOD_PLATFORM}' (rp2350, esp32s3, host)")
endif()
