cmake_minimum_required(VERSION 3.13)

# Must be set before pico_sdk_import.cmake
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(neupod C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(hal/rp2350)
add_subdirectory(app)
