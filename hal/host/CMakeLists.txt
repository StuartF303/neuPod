# Host (desktop) HAL. SDL2 is optional; without it the simulator runs headless.
add_library(hal_host STATIC
  hal_system.c
)
target_include_directories(hal_host PUBLIC ${CMAKE_SOURCE_DIR}/hal/include)

add_executable(neupod main.c)
target_link_libraries(neupod neupod_app hal_host)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  message(STATUS "neuPod host: SDL2 ${SDL2_VERSION} found — simulator window enabled")
  target_compile_definitions(hal_host PUBLIC NEUPOD_HOST_SDL=1)
  target_link_libraries(hal_host PUBLIC SDL2::SDL2)
else()
  message(STATUS "neuPod host: SDL2 not found — headless build (apt install libsdl2-dev)")
endif()
