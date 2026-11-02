# Host (desktop) HAL. SDL2 is optional; without it the simulator runs headless.
add_library(hal_host STATIC
  hal_system.c
  hal_display.c
  hal_console.c
  hal_input.c
  hal_storage.c
  hal_i2c.c
  nfs_posix.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
  host_window.c
)
target_include_directories(hal_host PUBLIC ${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hal_host PRIVATE ${CMAKE_SOURCE_DIR}/hal/common)
target_link_libraries(hal_host PUBLIC neupod_clickwheel)
# Default disk image (override at run time with NEUPOD_DISK_IMAGE).
target_compile_definitions(hal_host PRIVATE
  NEUPOD_DEFAULT_DISK_IMAGE="${CMAKE_BINARY_DIR}/disk.img"
  NEUPOD_DEFAULT_FS_ROOT="${CMAKE_BINARY_DIR}/fsroot")

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

add_executable(storage_test storage_test_main.c)
target_link_libraries(storage_test neupod_storage_tests hal_host)

add_executable(i2c_test i2c_test_main.c)
target_link_libraries(i2c_test neupod_i2c_tests hal_host)

add_executable(now_playing now_playing_main.c)
target_link_libraries(now_playing neupod_app hal_host)

set_source_files_properties(hal_i2c.c i2c_test_main.c ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
  hal_system.c hal_display.c hal_console.c hal_input.c hal_storage.c nfs_posix.c host_window.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
  main.c display_test_main.c now_playing_main.c storage_test_main.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
