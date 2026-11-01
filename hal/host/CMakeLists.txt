# Host (desktop) HAL. SDL2 is optional; without it the simulator runs headless.
add_library(hal_host STATIC
  hal_system.c
  hal_display.c
  hal_input.c
  hal_storage.c
  host_window.c
)
target_include_directories(hal_host PUBLIC ${CMAKE_SOURCE_DIR}/hal/include)
target_link_libraries(hal_host PUBLIC neupod_clickwheel)
# Default disk image (override at run time with NEUPOD_DISK_IMAGE).
target_compile_definitions(hal_host PRIVATE
  NEUPOD_DEFAULT_DISK_IMAGE="${CMAKE_BINARY_DIR}/disk.img")

add_executable(neupod main.c)
# hal_host after neupod_app: the app's static libs reference HAL symbols.
target_link_libraries(neupod neupod_app hal_host)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  message(STATUS "neuPod host: SDL2 ${SDL2_VERSION} found — simulator window enabled")
  target_compile_definitions(hal_host PUBLIC NEUPOD_HOST_SDL=1)
  target_link_libraries(hal_host PUBLIC SDL2::SDL2)
else()
  message(STATUS "neuPod host: SDL2 not found — headless build (apt install libsdl2-dev)")
endif()

add_executable(display_test display_test_main.c)
target_link_libraries(display_test neupod_app hal_host)

set_source_files_properties(hal_system.c hal_display.c hal_input.c hal_storage.c host_window.c
  main.c display_test_main.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
