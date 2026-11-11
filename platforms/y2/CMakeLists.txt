# Innioasis Y2 (MT6582, Linux) platform. Entered from the top-level CMakeLists.txt after the FreeRTOS POSIX port and app/.
#
# The Y2 HAL sources are compiled straight into the executable, not into a static library: the executable's own
# objects are linked first, so hal_battery_* here wins over the MAX17048 driver in neupod_drivers (an archive member is
# only pulled in for symbols that are still undefined). Portable logic (evdev decoding, SD device selection, sysfs
# battery parsing, pixel conversion) lives in hal/y2_*.c and is also built by tests/CMakeLists.txt with the PC compiler.
set(Y2_HAL ${CMAKE_CURRENT_LIST_DIR}/hal)
set(NEUPOD_Y2_LOGIC_SOURCES
  ${Y2_HAL}/y2_input.c
  ${Y2_HAL}/y2_sd.c
  ${Y2_HAL}/y2_power.c
  ${Y2_HAL}/y2_fb.c
)
set(NEUPOD_Y2_HAL_SOURCES
  ${NEUPOD_Y2_LOGIC_SOURCES}
  ${Y2_HAL}/hal_system.c
  ${Y2_HAL}/hal_display.c
  ${Y2_HAL}/hal_console.c
  ${Y2_HAL}/hal_input.c
  ${Y2_HAL}/hal_storage.c
  ${Y2_HAL}/hal_battery.c
  ${Y2_HAL}/hal_audio.c
  ${Y2_HAL}/hal_radio.c
  ${Y2_HAL}/hal_i2c.c
  ${CMAKE_SOURCE_DIR}/hal/host/hal_core.c                 # core 1 = a native thread, portable POSIX
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
  ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c             # the file API on FatFs, as on the Pico (the host uses nfs_posix.c)
)

add_executable(neupod main.c ${NEUPOD_Y2_HAL_SOURCES})
target_include_directories(neupod PRIVATE ${Y2_HAL} ${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_SOURCE_DIR}/hal/common)
set_source_files_properties(main.c ${NEUPOD_Y2_HAL_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
# neupod_fatfs: FatFs + diskio over hal_storage (the raw block device). Libraries after the HAL objects.
target_link_libraries(neupod PRIVATE neupod_runtime neupod_drivers neupod_fatfs)

# A smaller image to copy over USB; the unstripped one stays in the build tree for debugging.
add_custom_command(TARGET neupod POST_BUILD
  COMMAND ${CMAKE_STRIP} -o $<TARGET_FILE:neupod>.stripped $<TARGET_FILE:neupod>
  COMMENT "Stripped copy: neupod.stripped")
