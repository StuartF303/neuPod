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

add_executable(test_bt_sim test_bt_sim.c)
target_link_libraries(test_bt_sim hal_host)
add_test(NAME bt_sim COMMAND test_bt_sim)
set_source_files_properties(test_bt_sim.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

if(NEUPOD_HAVE_MBEDTLS)
  add_executable(test_secret test_secret.c)
  target_link_libraries(test_secret neupod_settings hal_host)
  add_test(NAME secret COMMAND test_secret)
  set_source_files_properties(test_secret.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
endif()

add_executable(test_settings test_settings.c)
target_link_libraries(test_settings neupod_settings hal_host) # hal_host last: nfs and hal_storage
add_test(NAME settings COMMAND test_settings)
set_tests_properties(settings PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_settings.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

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

# File-browser fixture: a few real folders/files plus a folder of 300 files (more than the browser keeps).
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fsroot/ DESTINATION ${CMAKE_BINARY_DIR}/fsroot_fixture)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fsroot_fixture/many)
# A blank "card" so the storage boot check passes on the host (the file browser needs it).
execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/test_disk.img bs=1024 count=2048 status=none)
foreach(I RANGE 1 300)
  file(WRITE ${CMAKE_BINARY_DIR}/fsroot_fixture/many/file_${I}.txt "x")
endforeach()
add_test(NAME runtime_smoke
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour --snapshots ${CMAKE_BINARY_DIR}/snapshots)
set_tests_properties(runtime_smoke PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_fixture;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 120)

add_test(NAME input_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-input --snapshots ${CMAKE_BINARY_DIR}/snapshots_input)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_input)
set_tests_properties(input_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_fixture;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 120)

add_test(NAME wifi_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-wifi --snapshots ${CMAKE_BINARY_DIR}/snapshots_wifi)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_wifi)
set_tests_properties(wifi_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_wifi;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 120)

# Album art tour: Now Playing with a cover file (Cover.JPG: upper-case on purpose), with a JPEG inside the FLAC, and with art it cannot
# show (PNG inside, progressive cover.jpg beside). art_snapshot checks the pixels of the art box in the snapshots.
set(ART_FS ${CMAKE_BINARY_DIR}/fsroot_art)
file(MAKE_DIRECTORY "${ART_FS}/Album A" "${ART_FS}/Album B" "${ART_FS}/Album C")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/flac16_44k_st.flac "${ART_FS}/Album A/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/q420_600x600.jpg "${ART_FS}/Album A/Cover.JPG")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/flac_with_jpeg.flac "${ART_FS}/Album B/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/flac_with_png.flac "${ART_FS}/Album C/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/qprog_300x300.jpg "${ART_FS}/Album C/cover.jpg")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_art)
add_test(NAME art_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-art --snapshots ${CMAKE_BINARY_DIR}/snapshots_art)
set_tests_properties(art_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${ART_FS};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_SETUP art_snapshots
  TIMEOUT 120)
add_executable(test_art_snapshot test_art_snapshot.c)
set_source_files_properties(test_art_snapshot.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_test(NAME art_snapshot COMMAND test_art_snapshot ${CMAKE_BINARY_DIR}/snapshots_art)
set_tests_properties(art_snapshot PROPERTIES FIXTURES_REQUIRED art_snapshots)

# The bare-metal audio core (core 1) may not use the RTOS, stdio, malloc or SDK blocking primitives.
add_test(NAME audio_core_rules
  COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_core1_rules.cmake)

add_executable(test_boot_briefs test_boot_briefs.c)
target_link_libraries(test_boot_briefs lvgl)
target_include_directories(test_boot_briefs PRIVATE ${CMAKE_SOURCE_DIR}/app/runtime)
add_test(NAME boot_briefs COMMAND test_boot_briefs)
set_source_files_properties(test_boot_briefs.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_ui_logic test_ui_logic.c)
target_include_directories(test_ui_logic PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_link_libraries(test_ui_logic neupod_rt_core)
add_test(NAME ui_logic COMMAND test_ui_logic)
set_source_files_properties(test_ui_logic.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_wheel_input test_wheel_input.c)
target_link_libraries(test_wheel_input neupod_rt_core)
add_test(NAME wheel_input COMMAND test_wheel_input)
set_source_files_properties(test_wheel_input.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_wifi_logic test_wifi_logic.c)
target_link_libraries(test_wifi_logic neupod_rt_core hal_host)
add_test(NAME wifi_logic COMMAND test_wifi_logic)
set_source_files_properties(test_wifi_logic.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Audio maths (conversion, volume, sine/tone/scale generators) and the WAV parser.
add_executable(test_audio test_audio.c)
target_link_libraries(test_audio neupod_rt_core m)
add_test(NAME audio COMMAND test_audio)
set_source_files_properties(test_audio.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Codecs (FLAC / MP3 / ALAC), MP4 demuxer, tag parsers, arena, MD5 and the stream handshake, against vectors in tests/assets/audio.
set(NEUPOD_CODEC_TEST_VENDOR
  ${CMAKE_SOURCE_DIR}/app/audio/codec_dr.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/alac_dec.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/ag_dec.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/dp_dec.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/matrix_dec.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/ALACBitUtilities.c
  ${CMAKE_SOURCE_DIR}/app/third_party/alac/EndianPortable.c)
add_executable(test_codecs test_codecs.c ${CMAKE_SOURCE_DIR}/app/audio/codec.c ${NEUPOD_CODEC_TEST_VENDOR})
target_include_directories(test_codecs PRIVATE ${CMAKE_SOURCE_DIR}/app/include ${CMAKE_SOURCE_DIR}/app/third_party/alac)
target_compile_definitions(test_codecs PRIVATE NEUPOD_TEST_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/audio")
target_link_libraries(test_codecs neupod_rt_core Threads::Threads m)
set_source_files_properties(${NEUPOD_CODEC_TEST_VENDOR} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(test_codecs.c ${CMAKE_SOURCE_DIR}/app/audio/codec.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS};-Wno-multichar")
add_test(NAME codecs COMMAND test_codecs)

# Album art: the streamed JPEG decoder, source order and the picture-position parsing of the tag readers.
add_executable(test_art test_art.c)
target_compile_definitions(test_art PRIVATE NEUPOD_ART_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/art")
target_link_libraries(test_art neupod_rt_core)
set_source_files_properties(test_art.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_test(NAME art COMMAND test_art)

# The core-1 closure checker must be able to FAIL: a known-good and a known-bad disassembly fixture.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME core1_closure_selftest_good COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core1_closure.py --text ${CMAKE_SOURCE_DIR}/tests/assets/closure/good.txt)
add_test(NAME core1_closure_selftest_bad COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core1_closure.py --text ${CMAKE_SOURCE_DIR}/tests/assets/closure/bad.txt)
set_tests_properties(core1_closure_selftest_bad PROPERTIES WILL_FAIL TRUE)
# Debug-only SDK assert() edges are accepted only from the listed SDK callers; assert() in our own code still fails.
add_test(NAME core1_closure_selftest_sdk_assert_ok COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core1_closure.py --text ${CMAKE_SOURCE_DIR}/tests/assets/closure/good_sdk_assert.txt)
add_test(NAME core1_closure_selftest_own_assert_fails COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core1_closure.py --text ${CMAKE_SOURCE_DIR}/tests/assets/closure/bad_own_assert.txt)
set_tests_properties(core1_closure_selftest_own_assert_fails PROPERTIES WILL_FAIL TRUE)

# Optional AddressSanitizer + UBSan build of the codec / parser / handshake tests: cmake -DNEUPOD_HOST_ASAN=ON
# (runs the same hostile-input tests, which truncate and bit-flip every vector, under the sanitizers).
option(NEUPOD_HOST_ASAN "Also build test_codecs_asan with -fsanitize=address,undefined" OFF)
if(NEUPOD_HOST_ASAN)
  add_executable(test_codecs_asan test_codecs.c ${CMAKE_SOURCE_DIR}/app/audio/codec.c ${NEUPOD_CODEC_TEST_VENDOR}
    ${CMAKE_SOURCE_DIR}/app/audio/arena.c ${CMAKE_SOURCE_DIR}/app/audio/md5.c ${CMAKE_SOURCE_DIR}/app/audio/meta.c
    ${CMAKE_SOURCE_DIR}/app/audio/mp4.c ${CMAKE_SOURCE_DIR}/app/audio/wav.c ${CMAKE_SOURCE_DIR}/app/audio/stream_io.c
    ${CMAKE_SOURCE_DIR}/app/audio/spsc_ring.c ${CMAKE_SOURCE_DIR}/app/audio/audio_dsp.c)
  target_include_directories(test_codecs_asan PRIVATE ${CMAKE_SOURCE_DIR}/app/include ${CMAKE_SOURCE_DIR}/app/third_party/alac)
  target_compile_definitions(test_codecs_asan PRIVATE NEUPOD_TEST_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/audio")
  target_compile_options(test_codecs_asan PRIVATE -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift -fno-omit-frame-pointer -g -Wno-multichar) # wrapping arithmetic in the vendored decoders on corrupt data is benign on ARM and would drown real findings
  target_link_options(test_codecs_asan PRIVATE -fsanitize=address,undefined)
  target_link_libraries(test_codecs_asan Threads::Threads m)
  add_test(NAME codecs_asan COMMAND test_codecs_asan)
endif()

# Gapless playback: the real core-1 loop (audio_core.c, white box) with a fake sink, fed by the real core-0 stream controller from
# the vectors in tests/assets/audio; the sink output is compared with the tracks decoded on their own.
add_executable(test_gapless test_gapless.c
  ${CMAKE_SOURCE_DIR}/app/audio/audio_shared.c ${CMAKE_SOURCE_DIR}/app/audio/audio_ctl.c ${CMAKE_SOURCE_DIR}/app/audio/codec.c
  ${CMAKE_SOURCE_DIR}/app/audio/track_feed.c ${CMAKE_SOURCE_DIR}/app/audio/track_stream.c ${NEUPOD_CODEC_TEST_VENDOR})
target_include_directories(test_gapless PRIVATE ${CMAKE_SOURCE_DIR}/app/include ${CMAKE_SOURCE_DIR}/app/third_party/alac)
target_compile_definitions(test_gapless PRIVATE NEUPOD_TEST_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/audio")
target_link_libraries(test_gapless neupod_rt_core Threads::Threads m)
set_source_files_properties(test_gapless.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-multichar")
add_test(NAME gapless COMMAND test_gapless)
set_tests_properties(gapless PROPERTIES TIMEOUT 120)

# Play queue: folder order, skipping non-audio entries, next / previous rules, end of queue.
add_executable(test_play_queue test_play_queue.c)
target_link_libraries(test_play_queue neupod_rt_core)
add_test(NAME play_queue COMMAND test_play_queue)
set_source_files_properties(test_play_queue.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# art_service retargeted by a track change in the middle of a decode (gapless handoff): the old picture must not be published.
add_executable(test_art_service test_art_service.c ${CMAKE_SOURCE_DIR}/app/runtime/art_service.c)
target_compile_definitions(test_art_service PRIVATE NEUPOD_ART_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/art")
target_link_libraries(test_art_service neupod_rt_core neupod_storage_tests hal_host)
set_source_files_properties(test_art_service.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
add_test(NAME art_service COMMAND test_art_service)

# Player queue / gapless announcement bookkeeping against fakes of the audio core, storage and settings.
add_executable(test_player test_player.c ${CMAKE_SOURCE_DIR}/app/runtime/player.c)
target_link_libraries(test_player neupod_rt_core)
set_source_files_properties(test_player.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
add_test(NAME player COMMAND test_player)
