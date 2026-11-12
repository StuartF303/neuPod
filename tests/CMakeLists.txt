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

# Resume record over the real settings store: round trip through the store's write path, rate limiting, missing / changed file decisions.
add_executable(test_resume test_resume.c ${CMAKE_SOURCE_DIR}/app/runtime/resume.c)
target_compile_definitions(test_resume PRIVATE NEUPOD_TEST_ASSETS="${CMAKE_SOURCE_DIR}/tests/assets/audio")
target_link_libraries(test_resume neupod_settings neupod_rt_core hal_host)
add_test(NAME resume COMMAND test_resume)
set_tests_properties(resume PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_resume.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The remembered Bluetooth headset over the real settings store and the host Bluetooth simulator.
add_executable(test_bt_saved test_bt_saved.c ${CMAKE_SOURCE_DIR}/app/runtime/bt_saved.c)
target_link_libraries(test_bt_saved neupod_settings hal_host)
add_test(NAME bt_saved COMMAND test_bt_saved)
set_tests_properties(bt_saved PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_bt_saved.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Audio routing: Auto / Wired, the quiet change of output, pause on link loss, over the Bluetooth simulator and the real settings store.
add_executable(test_bt_reconnect test_bt_reconnect.c ${CMAKE_SOURCE_DIR}/app/runtime/bt_reconnect.c)
target_link_libraries(test_bt_reconnect hal_host)
add_test(NAME bt_reconnect COMMAND test_bt_reconnect)
set_source_files_properties(test_bt_reconnect.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
# The A2DP link policy (catch-up budget, bitpool ladder): pure, no BTstack.
add_executable(test_bt_link_policy test_bt_link_policy.c ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack/bt_link_policy.c)
target_include_directories(test_bt_link_policy PRIVATE ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack)
add_test(NAME bt_link_policy COMMAND test_bt_link_policy)
set_source_files_properties(test_bt_link_policy.c ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack/bt_link_policy.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_executable(test_bt_control test_bt_control.c ${CMAKE_SOURCE_DIR}/app/runtime/bt_control.c)
target_link_libraries(test_bt_control hal_host neupod_rt_core)
add_test(NAME bt_control COMMAND test_bt_control)
set_source_files_properties(test_bt_control.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_executable(test_audio_route test_audio_route.c ${CMAKE_SOURCE_DIR}/app/runtime/audio_route.c ${CMAKE_SOURCE_DIR}/app/audio/bt_pcm.c)
target_link_libraries(test_audio_route neupod_settings hal_host neupod_rt_core)
add_test(NAME audio_route COMMAND test_audio_route)
set_source_files_properties(test_audio_route.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

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
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_fixture;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img;NEUPOD_UI_AUDIT=1"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  RESOURCE_LOCK neupod_shared_card
  TIMEOUT 120)
# The layout audit (app/ui/ui_audit.c, hosted builds) also runs on every snapshot of this 320x240 tour. Proof that it ran: PASS needs
# its per-snapshot line and the clean end of the run (a PASS regex replaces the exit code check, so "status 0" stands in for it);
# "ui: AUDIT FAIL" fails the test.
if(NEUPOD_UI_HIRES)
  set_tests_properties(runtime_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "ui: audit '[^']*' 320x240 objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL")
endif()

add_test(NAME input_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-input --snapshots ${CMAKE_BINARY_DIR}/snapshots_input)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_input)
set_tests_properties(input_tour PROPERTIES
  RESOURCE_LOCK neupod_shared_card
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_fixture;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 120)

add_test(NAME wifi_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-wifi --snapshots ${CMAKE_BINARY_DIR}/snapshots_wifi)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_wifi)
set_tests_properties(wifi_tour PROPERTIES
  RESOURCE_LOCK neupod_shared_card
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_wifi;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 120)

# Bluetooth pairing tour (stage 2a): pair, remembered-headset rows and pane, Disconnect, Forget, a refused pairing; snapshots bt-pairing,
# bt-paired, bt-saved, settings-bt-saved, bt-disconnected, bt-forget-asks, bt-forgotten, bt-failed. The run fails if the script does not
# reach its last snapshot (the tour expects its 13 files).
# Stage 2b adds the audio half: the headset's stream opens, a track from the WAV album plays on Bluetooth (np-bt-badge), Settings > Bluetooth shows
# "Streaming" and the codec (bt-streaming), then the headset goes away (bt-dropped: the banner, the player paused). The tour pairs and keeps a
# headset, so the card's settings are wiped first (bt_clean), and it has its own card with the 20 s WAV album (like the seek tour).
add_test(NAME bt_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/fsroot_bt/.neupod)
set_tests_properties(bt_tour_reset PROPERTIES FIXTURES_SETUP bt_clean)
add_test(NAME bt_tour
  COMMAND neupod --run-ms 100000 --splash-hold-ms 1500 --tour-bt --snapshots ${CMAKE_BINARY_DIR}/snapshots_bt)
set_tests_properties(bt_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_bt;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_bt.img"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  FIXTURES_REQUIRED bt_clean
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  TIMEOUT 200)

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
# Settings persist as files under the fake card's .neupod folder: wipe them so the tour (which sets Repeat One) starts from defaults and
# leaves nothing behind for the next run.
add_test(NAME art_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${ART_FS}/.neupod)
set_tests_properties(art_tour_reset PROPERTIES FIXTURES_SETUP art_clean)
add_test(NAME art_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-art --snapshots ${CMAKE_BINARY_DIR}/snapshots_art)
set_tests_properties(art_tour PROPERTIES
  RESOURCE_LOCK neupod_shared_card
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_FS_ROOT=${ART_FS};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/test_disk.img"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED art_clean
  FIXTURES_SETUP art_snapshots
  TIMEOUT 120)
add_executable(test_art_snapshot test_art_snapshot.c)
set_source_files_properties(test_art_snapshot.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_test(NAME art_snapshot COMMAND test_art_snapshot ${CMAKE_BINARY_DIR}/snapshots_art)
set_tests_properties(art_snapshot PROPERTIES FIXTURES_REQUIRED art_snapshots)

# Now Playing seek mode (UI-7): a fake card with an album of three silent 20 s WAVs (seekable), an MP3 (seekable since UI-7) and a FLAC without a seek
# table (not seekable), the tour pauses a track, enters seek mode, scrubs, lets the 3 s idle timeout run, then presses Select on the MP3 (seek mode) and
# on the FLAC (stays on volume). test_seek_snapshot checks the pixels of
# the progress / volume bars in the snapshots at both metrics profiles (this 320x240 one and, in hosted hires builds, 480x360).
# The WAV is a 44-byte header (printf, octal escapes: 44.1 kHz, 16 bit, stereo, 3528000 data bytes) and zeros from dd, made at configure time.
function(neupod_seek_card NAME)
  set(FS ${CMAKE_BINARY_DIR}/fsroot_${NAME})
  file(MAKE_DIRECTORY "${FS}/Album" "${FS}/Other" "${FS}/Zflac")
  execute_process(COMMAND sh -c "printf 'RIFF\\144\\354\\065\\000WAVEfmt \\020\\000\\000\\000\\001\\000\\002\\000\\104\\254\\000\\000\\020\\261\\002\\000\\004\\000\\020\\000data\\100\\354\\065\\000' > '${FS}/Album/01.wav' && dd if=/dev/zero bs=1000 count=3528 status=none >> '${FS}/Album/01.wav' && cp '${FS}/Album/01.wav' '${FS}/Album/02.wav' && cp '${FS}/Album/01.wav' '${FS}/Album/03.wav'")
  file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/mp3_44k_st.mp3 "${FS}/Other/track.mp3")
  file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/flac16_44k_st.flac "${FS}/Zflac/track.flac") # no SEEKTABLE: Seek is not offered
  execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_${NAME}.img bs=1024 count=2048 status=none)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_${NAME})
endfunction()
add_executable(test_seek_snapshot test_seek_snapshot.c)
set_source_files_properties(test_seek_snapshot.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
neupod_seek_card(seek)
neupod_seek_card(bt) # the Bluetooth tour has the same WAV album
add_test(NAME seek_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/fsroot_seek/.neupod)
set_tests_properties(seek_tour_reset PROPERTIES FIXTURES_SETUP seek_clean)
add_test(NAME seek_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-seek --snapshots ${CMAKE_BINARY_DIR}/snapshots_seek)
set_tests_properties(seek_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_seek;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_seek.img"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED seek_clean
  FIXTURES_SETUP seek_snapshots
  TIMEOUT 120)
add_test(NAME seek_snapshot COMMAND test_seek_snapshot ${CMAKE_BINARY_DIR}/snapshots_seek 320)
set_tests_properties(seek_snapshot PROPERTIES FIXTURES_REQUIRED seek_snapshots)

if(NEUPOD_UI_HIRES) # (the layout audit and the 480x360 profile exist only in hosted builds with the hires UI)
# ---- 480x360 (the Y2 testbed panel, ADR-0018): the same tours on a host display set to 480x360 with the layout audit on --------------
# NEUPOD_DISPLAY_SIZE makes the host HAL a 480x360 panel; the UI then selects its 1.5x metrics profile. NEUPOD_UI_AUDIT=1 makes every
# snapshot also walk the widget tree: a widget cut by the panel or its container prints "ui: AUDIT FAIL" and fails the test.
# Separate fixture folders (and snapshot folders) so the 320x240 tours' stored settings and PPMs are never reused.
# Every hires test has its own fake card (folder and disk image), so they share nothing with each other or with the 320x240 tours and
# can run side by side (ctest -j; CI does).
set(HIRES_PASS "ui: audit '[^']*' 480x360 objects=[1-9][0-9]* violations=0.*host: run finished, status 0")
function(neupod_hires_card NAME)
  set(FS ${CMAKE_BINARY_DIR}/fsroot_${NAME})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fsroot/ DESTINATION ${FS})
  file(MAKE_DIRECTORY ${FS}/many)
  foreach(I RANGE 1 300)
    file(WRITE ${FS}/many/file_${I}.txt "x")
  endforeach()
  execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_${NAME}.img bs=1024 count=2048 status=none)
endfunction()
foreach(T tour:hires:tour tour-input:hires_input:input_tour tour-wifi:hires_wifi:wifi_tour tour-bt:hires_bt:bt_tour)
  string(REPLACE ":" ";" TL "${T}")
  list(GET TL 0 T_FLAG)
  list(GET TL 1 T_CARD)
  list(GET TL 2 T_NAME)
  if(T_CARD STREQUAL "hires_bt") # the Bluetooth tour pairs and plays: the WAV album card, settings wiped before each run
    neupod_seek_card(hires_bt)
    add_test(NAME hires_bt_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/fsroot_hires_bt/.neupod)
    set_tests_properties(hires_bt_tour_reset PROPERTIES FIXTURES_SETUP hires_bt_clean)
  else()
    neupod_hires_card(${T_CARD})
  endif()
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_${T_CARD})
  add_test(NAME hires_${T_NAME}
    COMMAND neupod --run-ms 100000 --splash-hold-ms 1500 --${T_FLAG} --snapshots ${CMAKE_BINARY_DIR}/snapshots_${T_CARD})
  set_tests_properties(hires_${T_NAME} PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_${T_CARD};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_${T_CARD}.img"
    PASS_REGULAR_EXPRESSION "${HIRES_PASS}"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 200)
  if(T_CARD STREQUAL "hires_bt")
    set_tests_properties(hires_${T_NAME} PROPERTIES FIXTURES_REQUIRED hires_bt_clean)
  endif()
endforeach()

# Album art at 480x360: its own fake card (settings wiped first, like art_tour), then the pixel checks of the 168 / 144 px art boxes.
set(HIRES_ART_FS ${CMAKE_BINARY_DIR}/fsroot_hires_art)
execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_hires_art.img bs=1024 count=2048 status=none)
file(MAKE_DIRECTORY "${HIRES_ART_FS}/Album A" "${HIRES_ART_FS}/Album B" "${HIRES_ART_FS}/Album C")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/flac16_44k_st.flac "${HIRES_ART_FS}/Album A/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/q420_600x600.jpg "${HIRES_ART_FS}/Album A/Cover.JPG")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/flac_with_jpeg.flac "${HIRES_ART_FS}/Album B/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/flac_with_png.flac "${HIRES_ART_FS}/Album C/track.flac")
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/art/qprog_300x300.jpg "${HIRES_ART_FS}/Album C/cover.jpg")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_hires_art)
add_test(NAME hires_art_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${HIRES_ART_FS}/.neupod)
set_tests_properties(hires_art_tour_reset PROPERTIES FIXTURES_SETUP hires_art_clean)
add_test(NAME hires_art_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-art --snapshots ${CMAKE_BINARY_DIR}/snapshots_hires_art)
set_tests_properties(hires_art_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${HIRES_ART_FS};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_hires_art.img"
  PASS_REGULAR_EXPRESSION "${HIRES_PASS}"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED hires_art_clean
  FIXTURES_SETUP hires_art_snapshots
  TIMEOUT 120)
# Seek mode at 480x360 (same tour, own card, the layout audit on every snapshot).
neupod_seek_card(hires_seek)
add_test(NAME hires_seek_tour_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/fsroot_hires_seek/.neupod)
set_tests_properties(hires_seek_tour_reset PROPERTIES FIXTURES_SETUP hires_seek_clean)
add_test(NAME hires_seek_tour
  COMMAND neupod --run-ms 45000 --splash-hold-ms 1500 --tour-seek --snapshots ${CMAKE_BINARY_DIR}/snapshots_hires_seek)
set_tests_properties(hires_seek_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_hires_seek;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_hires_seek.img"
  PASS_REGULAR_EXPRESSION "${HIRES_PASS}"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED hires_seek_clean
  FIXTURES_SETUP hires_seek_snapshots
  TIMEOUT 120)
add_test(NAME hires_seek_snapshot COMMAND test_seek_snapshot ${CMAKE_BINARY_DIR}/snapshots_hires_seek 480)
set_tests_properties(hires_seek_snapshot PROPERTIES FIXTURES_REQUIRED hires_seek_snapshots)
add_executable(test_art_snapshot_hires test_art_snapshot_hires.c)
set_source_files_properties(test_art_snapshot_hires.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
add_test(NAME hires_art_snapshot COMMAND test_art_snapshot_hires ${CMAKE_BINARY_DIR}/snapshots_hires_art)
set_tests_properties(hires_art_snapshot PROPERTIES FIXTURES_REQUIRED hires_art_snapshots)
endif()

# The bare-metal audio core (core 1) may not use the RTOS, stdio, malloc or SDK blocking primitives.
add_test(NAME audio_core_rules
  COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/check_core1_rules.cmake)

add_executable(test_boot_briefs test_boot_briefs.c)
target_link_libraries(test_boot_briefs lvgl)
target_include_directories(test_boot_briefs PRIVATE ${CMAKE_SOURCE_DIR}/app/runtime)
add_test(NAME boot_briefs COMMAND test_boot_briefs)
set_source_files_properties(test_boot_briefs.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# UI metrics profiles (app/ui/ui_metrics.c): layout invariants of every compiled-in resolution profile, 320x240 pinned to its historic values.
add_executable(test_ui_metrics test_ui_metrics.c ${CMAKE_SOURCE_DIR}/app/ui/ui_metrics.c)
target_link_libraries(test_ui_metrics lvgl neupod_rt_core)
target_include_directories(test_ui_metrics PRIVATE ${CMAKE_SOURCE_DIR}/app/runtime)
add_test(NAME ui_metrics COMMAND test_ui_metrics)
set_source_files_properties(test_ui_metrics.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

add_executable(test_ui_logic test_ui_logic.c)
target_include_directories(test_ui_logic PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_link_libraries(test_ui_logic neupod_rt_core)
add_test(NAME ui_logic COMMAND test_ui_logic)
set_source_files_properties(test_ui_logic.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Hold and the Power-key gestures (ADR-0020): the 2 s toggle, the 10 s power-off and its countdown, the short press, the slide switch.
add_executable(test_ui_hold test_ui_hold.c)
target_include_directories(test_ui_hold PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_link_libraries(test_ui_hold neupod_rt_core)
add_test(NAME ui_hold COMMAND test_ui_hold)
set_source_files_properties(test_ui_hold.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The Licences screen's data against LICENSE.md (the standing rule: licence notes stay up to date).
add_executable(test_licences test_licences.c)
target_include_directories(test_licences PRIVATE ${CMAKE_SOURCE_DIR}/app/ui)
target_compile_definitions(test_licences PRIVATE NEUPOD_LICENSE_MD="${CMAKE_SOURCE_DIR}/LICENSE.md")
target_link_libraries(test_licences neupod_rt_core)
add_test(NAME licences COMMAND test_licences)
set_source_files_properties(test_licences.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

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

# Music library tour (ADR-0017, L2): a card folder with a small tagged library (tests/tools/make_library_card.py writes it: real FLAC test vectors with
# their tags replaced, an MP3, an AAC track the player cannot decode, three 3 s WAVs). The tour builds the index on the first open (NEUPOD_LIB_SLOW_MS slows
# the scan so the progress screen is on show), walks every list, plays an album from its list and shuffles the library. PASS needs: the index built, a
# queue of 3 playing through its first handoff gaplessly from the library (the log line), and the layout audit clean on every snapshot.
function(neupod_library_card NAME)
  set(FS ${CMAKE_BINARY_DIR}/fsroot_${NAME})
  execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_${NAME}.img bs=1024 count=2048 status=none)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_${NAME})
  # the card folder is (re)made by a test so every run starts from no index and the default settings
  add_test(NAME ${NAME}_card COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_library_card.py ${FS} ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio)
  set_tests_properties(${NAME}_card PROPERTIES FIXTURES_SETUP ${NAME}_card)
endfunction()
# --run-ms is the end of the run (the tour must have finished its script by then, or the snapshot count check fails): the script itself takes ~55 s of wall
# clock on an idle machine, so 60 s flaked on a loaded CI runner; 80 s leaves headroom.
set(LIB_GAPLESS "player: track 2/3 .*\\(gapless\\)")
neupod_library_card(library)
add_test(NAME library_tour
  COMMAND neupod --run-ms 140000 --splash-hold-ms 1500 --tour-library --snapshots ${CMAKE_BINARY_DIR}/snapshots_library)
set_tests_properties(library_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_UI_AUDIT=1;NEUPOD_LIB_SLOW_MS=100;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_library;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_library.img"
  PASS_REGULAR_EXPRESSION "library: build finished in [0-9]+ ms: 16 tracks.*${LIB_GAPLESS}.*host: run finished, status 0"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED library_card
  TIMEOUT 200)
if(NEUPOD_UI_HIRES)
  set_tests_properties(library_tour PROPERTIES PASS_REGULAR_EXPRESSION
    "library: build finished in [0-9]+ ms: 16 tracks.*${LIB_GAPLESS}.*ui: audit '[^']*' 320x240 objects=[1-9][0-9]* violations=0.*host: run finished, status 0")
  neupod_library_card(hires_library)
  add_test(NAME hires_library_tour
    COMMAND neupod --run-ms 140000 --splash-hold-ms 1500 --tour-library --snapshots ${CMAKE_BINARY_DIR}/snapshots_hires_library)
  set_tests_properties(hires_library_tour PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_LIB_SLOW_MS=100;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_hires_library;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_hires_library.img"
    PASS_REGULAR_EXPRESSION "library: build finished in [0-9]+ ms: 16 tracks.*${LIB_GAPLESS}.*ui: audit '[^']*' 480x360 objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FIXTURES_REQUIRED hires_library_card
    TIMEOUT 200)
endif()

# The same on a write-protected card (NEUPOD_DISK_READ_ONLY=1): "Library needs a writable card" from the first open and from Update Library, and no scan is started.
neupod_library_card(library_ro)
add_test(NAME library_ro_tour
  COMMAND neupod --run-ms 30000 --splash-hold-ms 1500 --tour-library-ro --snapshots ${CMAKE_BINARY_DIR}/snapshots_library_ro)
set_tests_properties(library_ro_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_UI_AUDIT=1;NEUPOD_DISK_READ_ONLY=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_library_ro;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_library_ro.img"
  PASS_REGULAR_EXPRESSION "host: run finished, status 0"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL;library: build started"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED library_ro_card
  TIMEOUT 90)
if(NEUPOD_UI_HIRES)
  set_tests_properties(library_ro_tour PROPERTIES PASS_REGULAR_EXPRESSION
    "ui: audit 'lib-needs-writable' 320x240 objects=[1-9][0-9]* violations=0.*ui: audit 'lib-update-needs-writable' 320x240 objects=[1-9][0-9]* violations=0.*host: run finished, status 0")
  neupod_library_card(hires_library_ro)
  add_test(NAME hires_library_ro_tour
    COMMAND neupod --run-ms 30000 --splash-hold-ms 1500 --tour-library-ro --snapshots ${CMAKE_BINARY_DIR}/snapshots_hires_library_ro)
  set_tests_properties(hires_library_ro_tour PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_DISK_READ_ONLY=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_hires_library_ro;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_hires_library_ro.img"
    PASS_REGULAR_EXPRESSION "ui: audit 'lib-needs-writable' 480x360 objects=[1-9][0-9]* violations=0.*ui: audit 'lib-update-needs-writable' 480x360 objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL;library: build started"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FIXTURES_REQUIRED hires_library_ro_card
    TIMEOUT 90)
endif()

# Text beyond ASCII (UI-17): Files and Now Playing on a card with accented / U+00BF / CJK / emoji names and tags (tests/tools/make_text_card.py).
# The snapshots are for the eye; the test checks the run finished and the layout audit passed on every snapshot (at both profiles).
function(neupod_text_card NAME)
  set(FS ${CMAKE_BINARY_DIR}/fsroot_${NAME})
  execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_${NAME}.img bs=1024 count=2048 status=none)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_${NAME})
  add_test(NAME ${NAME}_card COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_text_card.py ${FS} ${CMAKE_CURRENT_SOURCE_DIR}/assets/audio)
  set_tests_properties(${NAME}_card PROPERTIES FIXTURES_SETUP ${NAME}_card)
endfunction()
neupod_text_card(text)
add_test(NAME text_tour
  COMMAND neupod --run-ms 30000 --splash-hold-ms 1500 --tour-text --snapshots ${CMAKE_BINARY_DIR}/snapshots_text)
set_tests_properties(text_tour PROPERTIES
  ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_text;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_text.img"
  PASS_REGULAR_EXPRESSION "host: run finished, status 0"
  FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  FIXTURES_REQUIRED text_card
  TIMEOUT 90)
if(NEUPOD_UI_HIRES)
  set_tests_properties(text_tour PROPERTIES PASS_REGULAR_EXPRESSION
    "ui: audit 'text-files' 320x240 objects=[1-9][0-9]* violations=0.*ui: audit 'text-folder' 320x240 objects=[1-9][0-9]* violations=0.*ui: audit 'text-nowplaying' 320x240 objects=[1-9][0-9]* violations=0.*host: run finished, status 0")
  neupod_text_card(hires_text)
  add_test(NAME hires_text_tour
    COMMAND neupod --run-ms 30000 --splash-hold-ms 1500 --tour-text --snapshots ${CMAKE_BINARY_DIR}/snapshots_hires_text)
  set_tests_properties(hires_text_tour PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;NEUPOD_DISPLAY_SIZE=480x360;NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_hires_text;NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_hires_text.img"
    PASS_REGULAR_EXPRESSION "ui: audit 'text-files' 480x360 objects=[1-9][0-9]* violations=0.*ui: audit 'text-folder' 480x360 objects=[1-9][0-9]* violations=0.*ui: audit 'text-nowplaying' 480x360 objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FIXTURES_REQUIRED hires_text_card
    TIMEOUT 90)
endif()

# Hold (ADR-0020): the Power key simulated by the tour (pwr:down / pwr:up / sleep:MS), the lock overlay, the countdown, the host's simulated power-off,
# the hardware switch, the screen-off stage with auto-lock, and the new Display rows. The PASS pattern follows the whole story in log order and the audit
# of the last snapshot; "AUDIT FAIL" fails it. Own fake card whose settings are wiped first (the tour turns Auto-lock off at the end).
function(neupod_hold_card NAME)
  set(FS ${CMAKE_BINARY_DIR}/fsroot_${NAME})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/fsroot/ DESTINATION ${FS})
  execute_process(COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk_${NAME}.img bs=1024 count=2048 status=none)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/snapshots_${NAME})
  add_test(NAME ${NAME}_reset COMMAND ${CMAKE_COMMAND} -E remove_directory ${FS}/.neupod)
  set_tests_properties(${NAME}_reset PROPERTIES FIXTURES_SETUP ${NAME}_clean)
endfunction()
function(neupod_hold_tour NAME CARD SIZE_ENV SIZE)
  add_test(NAME ${NAME}
    COMMAND neupod --run-ms 90000 --splash-hold-ms 1500 --tour-hold --snapshots ${CMAKE_BINARY_DIR}/snapshots_${CARD})
  set_tests_properties(${NAME} PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;${SIZE_ENV}NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_${CARD};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_${CARD}.img"
    PASS_REGULAR_EXPRESSION "ui: audit 'hold-ring' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: hold on.*ui: audit 'hold-locked' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: audit 'hold-swallowed' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: hold off.*ui: audit 'hold-countdown' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: hold on.*ui: power-off requested.*host: power-off requested.*ui: hold off.*ui: hold on.*ui: backlight off 0%.*ui: audit 'hold-display' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: audit 'hold-autolock-off' ${SIZE} objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FIXTURES_REQUIRED ${CARD}_clean
    TIMEOUT 150)
endfunction()
neupod_hold_card(hold)
neupod_hold_tour(hold_tour hold "" "320x240")
if(NEUPOD_UI_HIRES)
  neupod_hold_card(hires_hold)
  neupod_hold_tour(hires_hold_tour hires_hold "NEUPOD_DISPLAY_SIZE=480x360;" "480x360")
endif()

# Settings > About > Licences at both profiles: the lists and the longest notice, with the layout audit on every snapshot.
function(neupod_licence_tour NAME CARD SIZE_ENV SIZE)
  add_test(NAME ${NAME}
    COMMAND neupod --run-ms 30000 --splash-hold-ms 1500 --tour-licences --snapshots ${CMAKE_BINARY_DIR}/snapshots_${CARD})
  set_tests_properties(${NAME} PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;${SIZE_ENV}NEUPOD_UI_AUDIT=1;NEUPOD_FS_ROOT=${CMAKE_BINARY_DIR}/fsroot_${CARD};NEUPOD_DISK_IMAGE=${CMAKE_BINARY_DIR}/disk_${CARD}.img"
    PASS_REGULAR_EXPRESSION "ui: audit 'about' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: audit 'licences' ${SIZE} objects=[1-9][0-9]* violations=0.*ui: audit 'licence-btstack' ${SIZE} objects=[1-9][0-9]* violations=0.*host: run finished, status 0"
    FAIL_REGULAR_EXPRESSION "AUDIT FAIL"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FIXTURES_REQUIRED ${CARD}_clean
    TIMEOUT 90)
endfunction()
neupod_hold_card(licences)
neupod_licence_tour(licences_tour licences "" "320x240")
if(NEUPOD_UI_HIRES)
  neupod_hold_card(hires_licences)
  neupod_licence_tour(hires_licences_tour hires_licences "NEUPOD_DISPLAY_SIZE=480x360;" "480x360")
endif()

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
  ${CMAKE_SOURCE_DIR}/app/audio/bt_pcm.c ${CMAKE_SOURCE_DIR}/app/audio/track_feed.c ${CMAKE_SOURCE_DIR}/app/audio/track_stream.c ${NEUPOD_CODEC_TEST_VENDOR})
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
target_compile_definitions(test_player PRIVATE RESUME_WRITE_WHILE_PLAYING=1) # the periodic path is compiled out in the product build; test it here
target_link_libraries(test_player neupod_rt_core)
set_source_files_properties(test_player.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
add_test(NAME player COMMAND test_player)

add_executable(test_haptic_logic test_haptic_logic.c)
target_link_libraries(test_haptic_logic neupod_rt_core)
add_test(NAME haptic_logic COMMAND test_haptic_logic)
set_source_files_properties(test_haptic_logic.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# Innioasis Y2 portable logic (platforms/y2/hal/y2_*.c), built with the PC compiler: evdev decoding and the wheel mapping,
# SD device selection against a fake sysfs, power_supply parsing, RGB565 -> XRGB8888 and the page flipper.
add_library(neupod_y2_logic STATIC
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_input.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_sd.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_power.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_fb.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_sysfs.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_poweroff.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_charge.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_haptic.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_bt_addr.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_bt_tlv.c)
target_include_directories(neupod_y2_logic PUBLIC ${CMAKE_SOURCE_DIR}/platforms/y2/hal ${CMAKE_SOURCE_DIR}/hal/include)
set_source_files_properties(${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_input.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_sd.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_power.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_fb.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_sysfs.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_poweroff.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_charge.c
  ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_haptic.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_bt_addr.c ${CMAKE_SOURCE_DIR}/platforms/y2/hal/y2_bt_tlv.c
  test_y2_haptic.c test_y2_bt_addr.c test_y2_bt_tlv.c
  test_y2_charge.c test_y2_input.c test_y2_sd.c test_y2_power.c test_y2_fb.c test_y2_sysfs.c test_y2_poweroff.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
foreach(T y2_input y2_sd y2_power y2_fb y2_sysfs y2_poweroff y2_charge y2_haptic y2_bt_addr y2_bt_tlv)
  add_executable(test_${T} test_${T}.c)
  target_link_libraries(test_${T} neupod_y2_logic neupod_rt_core) # neupod_rt_core: input_events_diff (the real app mapping)
  add_test(NAME ${T} COMMAND test_${T})
endforeach()

# The Y2-only capability re-arm logic: capabilities.c compiled on its own with NEUPOD_PLATFORM_Y2 (the host build has it off).
add_executable(test_caps_rearm test_caps_rearm.c ${CMAKE_SOURCE_DIR}/app/runtime/capabilities.c)
target_compile_definitions(test_caps_rearm PRIVATE NEUPOD_PLATFORM_Y2=1)
target_include_directories(test_caps_rearm PRIVATE ${CMAKE_SOURCE_DIR}/app/include)
add_test(NAME caps_rearm COMMAND test_caps_rearm)

# Music library index (ADR-0017, L1): collation, new tag fields, scanner + reader on a FatFs disk image, swap crash safety.
# nfs.h on FatFs (the backend the Pico and the Y2 use) instead of the host's POSIX one: the library's writes go through its write scope.
add_executable(test_library test_library.c ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c)
target_link_libraries(test_library neupod_library_test neupod_storage_tests neupod_fatfs neupod_rt_core hal_host) # hal_host last: diskio references hal_storage
add_test(NAME library COMMAND test_library)
set_tests_properties(library PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_library.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The Music lists and queue ranges over a small tagged library (library_view.h), and every queue entry's path (lib_resolve).
add_executable(test_library_view test_library_view.c ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c)
target_link_libraries(test_library_view neupod_library neupod_storage_tests neupod_fatfs neupod_rt_core hal_host)
add_test(NAME library_view COMMAND test_library_view)
set_tests_properties(library_view PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_library_view.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The library service (the storage task's half of the Music screens): protocol, row window, build and its refusals, resolver. library_service.c is
# compiled in with fakes for the player, the audio core and the storage queue.
add_executable(test_library_service test_library_service.c ${CMAKE_SOURCE_DIR}/app/runtime/library_service.c ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c)
target_link_libraries(test_library_service neupod_library neupod_storage_tests neupod_fatfs neupod_rt_core hal_host)
add_test(NAME library_service COMMAND test_library_service)
set_tests_properties(library_service PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_source_files_properties(test_library_service.c ${CMAKE_SOURCE_DIR}/app/runtime/library_service.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# The same at scale: 100,000 files on a disk image, 32 KB of scratch. About a minute; label "slow" (ctest -LE slow skips it).
add_executable(test_library_big test_library_big.c ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c)
target_link_libraries(test_library_big neupod_library neupod_storage_tests neupod_fatfs neupod_rt_core hal_host)
add_test(NAME library_big COMMAND test_library_big)
set_tests_properties(library_big PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} LABELS slow TIMEOUT 900)
set_source_files_properties(test_library_big.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# ui_frame_input() routing of the transport keys (the real ui_frame.c with fakes for the player and services): consumed keys stay with the screen.
add_executable(test_ui_frame_keys test_ui_frame_keys.c ${CMAKE_SOURCE_DIR}/app/ui/ui_metrics.c)
target_include_directories(test_ui_frame_keys PRIVATE ${CMAKE_SOURCE_DIR}/app/ui ${CMAKE_SOURCE_DIR}/app/runtime)
target_link_libraries(test_ui_frame_keys neupod_rt_core lvgl)
add_test(NAME ui_frame_keys COMMAND test_ui_frame_keys)

# The Y2 Bluetooth init script against fake /dev, /run and /proc/devices (needs only /bin/sh).
add_test(NAME y2_s80_mtk_bt COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/test_y2_s80_mtk_bt.sh
  ${CMAKE_SOURCE_DIR}/testbed/y2/buildroot/board/y2/overlay/etc/init.d/S80mtk-bt)
