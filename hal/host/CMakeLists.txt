# Host (desktop) HAL. SDL2 is optional; without it the simulator runs headless.
add_library(hal_host STATIC
  hal_system.c
  hal_display.c
  hal_console.c
  hal_input.c
  hal_storage.c
  hal_i2c.c
  hal_audio.c
  hal_core.c
  hal_radio.c
  nfs_posix.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
  host_window.c
)
target_include_directories(hal_host PUBLIC ${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(hal_host PRIVATE ${CMAKE_SOURCE_DIR}/hal/common)
target_link_libraries(hal_host PUBLIC neupod_clickwheel Threads::Threads)
# Default disk image (override at run time with NEUPOD_DISK_IMAGE).
target_compile_definitions(hal_host PRIVATE
  NEUPOD_DEFAULT_DISK_IMAGE="${CMAKE_BINARY_DIR}/disk.img"
  NEUPOD_DEFAULT_FS_ROOT="${CMAKE_BINARY_DIR}/fsroot")

add_executable(neupod main.c)
# hal_host after the app libs: they reference HAL symbols.
target_link_libraries(neupod neupod_runtime neupod_drivers hal_host)

# -DNEUPOD_HOST_SDL=OFF forces the headless build even when SDL2 is installed.
option(NEUPOD_HOST_SDL "Build the host simulator window with SDL2 when available" ON)
if(NEUPOD_HOST_SDL)
  find_package(SDL2 QUIET)
endif()
if(NEUPOD_HOST_SDL AND SDL2_FOUND)
  message(STATUS "neuPod host: SDL2 ${SDL2_VERSION} found — simulator window enabled")
  target_compile_definitions(hal_host PUBLIC NEUPOD_HOST_SDL=1)
  target_link_libraries(hal_host PUBLIC SDL2::SDL2)
else()
  message(STATUS "neuPod host: headless build (SDL2 not found or NEUPOD_HOST_SDL=OFF; apt install libsdl2-dev)")
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
  hal_system.c hal_display.c hal_console.c hal_input.c hal_storage.c hal_audio.c hal_core.c hal_radio.c nfs_posix.c host_window.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
  main.c display_test_main.c now_playing_main.c storage_test_main.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The crypto library needs hal_chip_id() / hal_random() (listed after it on the link line).
target_link_libraries(neupod_crypto PUBLIC hal_host)
