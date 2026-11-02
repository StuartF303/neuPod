# Host unit tests. Run with: ctest --test-dir build/host --output-on-failure
add_executable(test_clickwheel test_clickwheel.c)
target_link_libraries(test_clickwheel neupod_clickwheel)
add_test(NAME clickwheel COMMAND test_clickwheel)

add_executable(test_fatfs test_fatfs.c)
# hal_host last: neupod_fatfs's diskio references hal_storage.
target_link_libraries(test_fatfs neupod_fatfs hal_host)
add_test(NAME fatfs COMMAND test_fatfs)
set_tests_properties(fatfs PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_gfx test_gfx.c)
target_link_libraries(test_gfx neupod_app hal_host) # hal_host last: gfx_strip references hal_display
add_test(NAME gfx COMMAND test_gfx)

add_executable(test_storage_decode test_storage_decode.c)
target_link_libraries(test_storage_decode hal_host)
target_include_directories(test_storage_decode PRIVATE ${CMAKE_SOURCE_DIR}/hal/common)
add_test(NAME storage_decode COMMAND test_storage_decode)

add_executable(test_nfs test_nfs.c)
target_link_libraries(test_nfs neupod_storage_tests hal_host)
add_test(NAME nfs COMMAND test_nfs)
set_tests_properties(nfs PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set_source_files_properties(test_nfs.c test_clickwheel.c test_fatfs.c test_gfx.c test_storage_decode.c
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_i2c test_i2c.c)
target_link_libraries(test_i2c hal_host)
add_test(NAME i2c COMMAND test_i2c)
set_source_files_properties(test_i2c.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_max17048 test_max17048.c)
target_link_libraries(test_max17048 neupod_drivers hal_host)
add_test(NAME max17048 COMMAND test_max17048)

add_executable(test_drv2605l test_drv2605l.c)
target_link_libraries(test_drv2605l neupod_drivers hal_host)
add_test(NAME drv2605l COMMAND test_drv2605l)
set_source_files_properties(test_max17048.c test_drv2605l.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_spsc_ring test_spsc_ring.c)
target_link_libraries(test_spsc_ring neupod_rt_core Threads::Threads)
add_test(NAME spsc_ring COMMAND test_spsc_ring)

add_executable(test_seqlock test_seqlock.c)
target_include_directories(test_seqlock PRIVATE ${CMAKE_SOURCE_DIR}/app/include)
target_link_libraries(test_seqlock Threads::Threads)
add_test(NAME seqlock COMMAND test_seqlock)
set_source_files_properties(test_seqlock.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Optional ThreadSanitizer variant of the lock-free ring test: cmake -DNEUPOD_HOST_TSAN=ON
# (the seqlock test is excluded: its payload read is a data race by design, discarded by the retry).
option(NEUPOD_HOST_TSAN "Also build test_spsc_ring_tsan with -fsanitize=thread" OFF)
if(NEUPOD_HOST_TSAN)
  add_executable(test_spsc_ring_tsan test_spsc_ring.c ${CMAKE_SOURCE_DIR}/app/audio/spsc_ring.c)
  target_include_directories(test_spsc_ring_tsan PRIVATE ${CMAKE_SOURCE_DIR}/app/include)
  target_compile_options(test_spsc_ring_tsan PRIVATE -fsanitize=thread -g)
  target_link_options(test_spsc_ring_tsan PRIVATE -fsanitize=thread)
  target_link_libraries(test_spsc_ring_tsan Threads::Threads)
  # TSan needs ASLR off on recent kernels ("unexpected memory mapping"), hence setarch -R.
  find_program(NEUPOD_SETARCH setarch)
  if(NEUPOD_SETARCH)
    add_test(NAME spsc_ring_tsan COMMAND ${NEUPOD_SETARCH} ${CMAKE_SYSTEM_PROCESSOR} -R $<TARGET_FILE:test_spsc_ring_tsan>)
  else()
    add_test(NAME spsc_ring_tsan COMMAND test_spsc_ring_tsan)
  endif()
endif()

add_executable(test_input_events test_input_events.c)
target_link_libraries(test_input_events neupod_rt_core)
add_test(NAME input_events COMMAND test_input_events)
set_source_files_properties(test_spsc_ring.c test_input_events.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Boots the whole runtime headless (FreeRTOS POSIX port, LVGL, boot checks, audio stub) for ~8 s,
# writes the splash and home screens as PPM, and fails on a crash, a stalled audio task or missing
# snapshots. SDL_VIDEODRIVER=dummy keeps an SDL build from needing a display (and falls back to
# the headless framebuffer if SDL cannot start at all).
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots)
add_test(NAME runtime_smoke
  COMMAND neupod --run-ms 8000 --splash-hold-ms 1500 --snapshots ${CMAKE_BINARY_DIR}/snapshots)
set_tests_properties(runtime_smoke PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 60)

# The bare-metal audio core (core 1) may not use the RTOS, stdio, malloc or SDK blocking primitives.
add_test(NAME audio_core_rules
  COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_core1_rules.cmake)

add_executable(test_boot_briefs test_boot_briefs.c)
target_link_libraries(test_boot_briefs lvgl)
target_include_directories(test_boot_briefs PRIVATE ${CMAKE_SOURCE_DIR}/app/runtime)
add_test(NAME boot_briefs COMMAND test_boot_briefs)
set_source_files_properties(test_boot_briefs.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
