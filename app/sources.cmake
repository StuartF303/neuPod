# Portable application sources, shared by every platform build (incl. ESP-IDF components).
set(NEUPOD_APP_DIR ${CMAKE_CURRENT_LIST_DIR})

# Split lists so the host/RP2350 builds can make separate static libs; NEUPOD_APP_SOURCES is the
# union for build systems (ESP-IDF) that compile everything into one component.
set(NEUPOD_CORE_SOURCES
  ${NEUPOD_APP_DIR}/app.c
)
set(NEUPOD_GFX_SOURCES
  ${NEUPOD_APP_DIR}/gfx/gfx.c
  ${NEUPOD_APP_DIR}/gfx/gfx_strip.c       # needs hal_display (gfx_sink_display)
  ${NEUPOD_APP_DIR}/gfx/now_playing.c
  ${NEUPOD_APP_DIR}/gfx/fonts/font8x8.c
  ${NEUPOD_APP_DIR}/gfx/fonts/font16.c
)
set(NEUPOD_DEMO_SOURCES
  ${NEUPOD_APP_DIR}/tests/display_tests.c  # needs hal_display + hal_console
  ${NEUPOD_APP_DIR}/tests/assets/test_image.c
)
set(NEUPOD_STORAGE_TEST_SOURCES
  ${NEUPOD_APP_DIR}/tests/storage_tests.c  # needs hal_storage + hal_console (no FatFs headers)
  ${NEUPOD_APP_DIR}/storage/nfs_path.c    # portable half of the nfs file API
)
# Chip drivers over hal_i2c (portable; implement hal_battery.h / hal_haptic.h).
set(NEUPOD_DRIVER_SOURCES
  ${NEUPOD_APP_DIR}/drivers/max17048.c
  ${NEUPOD_APP_DIR}/drivers/drv2605l.c
)
set(NEUPOD_I2C_TEST_SOURCES
  ${NEUPOD_APP_DIR}/tests/i2c_tests.c      # needs hal_i2c + hal_console (portable)
)
# FatFs backend of nfs.h: RP2350 only, built against the SD library's ff.h (not in neupod_app).
set(NEUPOD_NFS_FATFS_SOURCES ${NEUPOD_APP_DIR}/storage/nfs_fatfs.c)
set(NEUPOD_CLICKWHEEL_SOURCES
  ${NEUPOD_APP_DIR}/input/clickwheel_frame.c
)
set(NEUPOD_FATFS_SOURCES
  ${NEUPOD_APP_DIR}/third_party/fatfs/ff.c
  ${NEUPOD_APP_DIR}/third_party/fatfs/ffunicode.c
  ${NEUPOD_APP_DIR}/third_party/fatfs/ffsystem.c
  ${NEUPOD_APP_DIR}/storage/diskio.c
)
# Runtime (FreeRTOS tasks, LVGL UI, boot checks, audio timing stub). Needs os.h's FreeRTOS backend
# and LVGL, so it is NOT part of NEUPOD_APP_SOURCES (the ESP-IDF skeleton keeps building app.c only).
set(NEUPOD_RT_CORE_SOURCES               # pure logic, no OS: host-testable on their own
  ${NEUPOD_APP_DIR}/audio/spsc_ring.c
  ${NEUPOD_APP_DIR}/input/input_events.c
  ${NEUPOD_APP_DIR}/ui/ui_logic.c
  ${NEUPOD_APP_DIR}/audio/audio_dsp.c
  ${NEUPOD_APP_DIR}/audio/wav.c
)
# Bare-metal audio core (core 1). Subject to the core-1 rules (cmake/check_core1_rules.cmake):
# no RTOS, stdio, malloc or SDK blocking primitives. audio_ctl.c is the core-0 side.
set(NEUPOD_AUDIO_CORE_SOURCES
  ${NEUPOD_APP_DIR}/audio/audio_core.c
  ${NEUPOD_APP_DIR}/audio/audio_shared.c
)
set(NEUPOD_RUNTIME_SOURCES
  ${NEUPOD_APP_DIR}/os/os_freertos.c
  ${NEUPOD_APP_DIR}/runtime/runtime.c
  ${NEUPOD_APP_DIR}/runtime/boot.c
  ${NEUPOD_APP_DIR}/runtime/capabilities.c
  ${NEUPOD_APP_DIR}/runtime/battery_store.c
  ${NEUPOD_APP_DIR}/runtime/panic.c
  ${NEUPOD_APP_DIR}/runtime/storage_service.c
  ${NEUPOD_APP_DIR}/runtime/haptic_service.c
  ${NEUPOD_APP_DIR}/runtime/player.c
  ${NEUPOD_APP_DIR}/storage/nfs_path.c   # portable half of the nfs API (the backend is in the HAL libs)
  ${NEUPOD_APP_DIR}/audio/audio_ctl.c
)
set(NEUPOD_UI_SOURCES
  ${NEUPOD_APP_DIR}/ui/ui.c
  ${NEUPOD_APP_DIR}/ui/ui_splash.c
  ${NEUPOD_APP_DIR}/ui/ui_frame.c
  ${NEUPOD_APP_DIR}/ui/ui_list.c
  ${NEUPOD_APP_DIR}/ui/ui_screens.c
  ${NEUPOD_APP_DIR}/ui/ui_files.c
  ${NEUPOD_APP_DIR}/ui/ui_audio.c
  ${NEUPOD_APP_DIR}/ui/ui_settings.c
)
set(NEUPOD_APP_SOURCES
  ${NEUPOD_CORE_SOURCES}
  ${NEUPOD_GFX_SOURCES}
  ${NEUPOD_DEMO_SOURCES}
  ${NEUPOD_STORAGE_TEST_SOURCES}
  ${NEUPOD_CLICKWHEEL_SOURCES}
  ${NEUPOD_FATFS_SOURCES}
)
# Warning flags for project code only (applied per source file; never to third_party or the SDK).
set(NEUPOD_WARN_FLAGS -Wall -Wextra -Wshadow)

set(NEUPOD_APP_INCLUDE_DIRS
  ${NEUPOD_APP_DIR}/include
  ${NEUPOD_APP_DIR}/third_party/fatfs
  ${NEUPOD_APP_DIR}/../hal/include
)
