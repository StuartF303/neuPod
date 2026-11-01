# Included by the top-level CMakeLists.txt BEFORE project() when NEUPOD_PLATFORM=rp2350.
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
