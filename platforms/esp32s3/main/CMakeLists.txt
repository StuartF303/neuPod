# The neuPod runtime as one ESP-IDF component: the portable app/ sources (the same lists the RP2350 `neupod` target links),
# LVGL, our FatFs, and the board HAL in hal/. Board: Waveshare ESP32-S3-Touch-AMOLED-2.41 (docs/esp32s3.md).
set(NEUPOD_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${NEUPOD_ROOT}/app/sources.cmake)

# --- LVGL (submodule lib/lvgl): every .c under src/, configured by app/ui/lv_conf.h (as app/CMakeLists.txt does) ---
set(NEUPOD_LVGL_DIR ${NEUPOD_ROOT}/lib/lvgl)
if(NOT EXISTS ${NEUPOD_LVGL_DIR}/src/lvgl.h AND NOT EXISTS ${NEUPOD_LVGL_DIR}/lvgl.h)
  message(FATAL_ERROR "LVGL submodule missing. Run: git submodule update --init lib/lvgl")
endif()
file(GLOB_RECURSE NEUPOD_LVGL_SOURCES ${NEUPOD_LVGL_DIR}/src/*.c)
# The UI's own fonts (generated, app/ui/fonts). All of them: flash is plentiful here (16 MB) and the set follows the UI profile.
file(GLOB NEUPOD_UI_FONT_SOURCES ${NEUPOD_APP_DIR}/ui/fonts/*.c)

# UI profile: one compiled-in profile, as app/CMakeLists.txt / the top-level CMakeLists.txt do for the other MCU builds.
if(NOT NEUPOD_UI_PROFILE) # IDF's requirements pass runs this file in script mode, without the project's cache
  set(NEUPOD_UI_PROFILE "600x450")
endif()
if(NEUPOD_UI_PROFILE STREQUAL "600x450")
  set(NEUPOD_UI_PROFILE_DEFS NEUPOD_UI_PROFILE_600=1)
elseif(NOT NEUPOD_UI_PROFILE STREQUAL "320x240")
  message(FATAL_ERROR "NEUPOD_UI_PROFILE must be 320x240 or 600x450 (got '${NEUPOD_UI_PROFILE}')")
endif()

# --- Secrets at rest: the same crypto_secret.c / key provider, over IDF's mbed TLS (hardware AES/SHA) ---
set(NEUPOD_CRYPTO_SOURCES
  ${NEUPOD_APP_DIR}/crypto/crypto_secret.c
  ${NEUPOD_APP_DIR}/crypto/key_provider_chipid.c
)

# --- Board HAL ---
set(NEUPOD_ESP_HAL_SOURCES
  hal/board.c
  hal/hal_system.c
  hal/hal_console.c
  hal/hal_core.c
  hal/hal_audio.c
  hal/hal_display.c
  hal/hal_touch.c
  hal/hal_input.c
  hal/hal_i2c.c
  hal/hal_storage.c
  hal/hal_radio.c
  ${NEUPOD_ROOT}/hal/common/hal_i2c_common.c
  ${NEUPOD_ROOT}/hal/common/hal_power_none.c   # no suspend yet (docs/adr/0021-power-sleep.md)
  ${NEUPOD_ROOT}/hal/common/hal_storage_common.c
)

if(NEUPOD_UI_BENCH)
  list(APPEND NEUPOD_ESP_HAL_SOURCES hal/ui_bench_mem.c)
  set(NEUPOD_BENCH_DEFS NEUPOD_UI_BENCH=1)
endif()

set(NEUPOD_PROJECT_SOURCES   # compiled with the project's warning flags
  ${NEUPOD_RT_CORE_SOURCES}
  ${NEUPOD_AUDIO_CORE_SOURCES}
  ${NEUPOD_RUNTIME_SOURCES}
  ${NEUPOD_UI_SOURCES}
  ${NEUPOD_SETTINGS_SOURCES}
  ${NEUPOD_LIBRARY_SOURCES}
  ${NEUPOD_DRIVER_SOURCES}        # MAX17048 / DRV2605L over hal_i2c: they probe, find nothing on this board and report absent
  ${NEUPOD_NFS_FATFS_SOURCES}
  ${NEUPOD_CRYPTO_SOURCES}
  ${NEUPOD_ESP_HAL_SOURCES}
  main.c
)
set(NEUPOD_THIRD_PARTY_SOURCES  # third party: no project warning flags
  ${NEUPOD_FATFS_SOURCES}         # FatFs + ff_alloc.c + diskio.c (over hal_storage)
  ${NEUPOD_CODEC_VENDOR_SOURCES}
  ${NEUPOD_APP_DIR}/third_party/tjpgd/tjpgd.c
  ${NEUPOD_UI_FONT_SOURCES}
  ${NEUPOD_LVGL_SOURCES}
)
# tjpgd.c is listed in NEUPOD_RT_CORE_SOURCES (project flags there): keep it in the third-party group only.
list(REMOVE_ITEM NEUPOD_PROJECT_SOURCES ${NEUPOD_APP_DIR}/third_party/tjpgd/tjpgd.c)
list(REMOVE_DUPLICATES NEUPOD_THIRD_PARTY_SOURCES)

idf_component_register(
  SRCS
    ${NEUPOD_PROJECT_SOURCES}
    ${NEUPOD_THIRD_PARTY_SOURCES}
  INCLUDE_DIRS
    "hal"
  LDFRAGMENTS
    "linker.lf"
  PRIV_REQUIRES
    driver
    esp_lcd
    esp_psram
    esp_timer
    esp_system
    esp_hw_support
    esp_rom
    freertos
    heap
    mbedtls
    sdmmc
)
# (no "fatfs" here: see ../CMakeLists.txt)

target_include_directories(${COMPONENT_LIB} PRIVATE
  ${NEUPOD_APP_INCLUDE_DIRS}                # app/include, the vendored FatFs, hal/include
  ${NEUPOD_ROOT}/hal/common
  ${NEUPOD_APP_DIR}/os                      # memory_budget.h
  ${NEUPOD_APP_DIR}/ui
  ${NEUPOD_APP_DIR}/third_party/tjpgd
  ${NEUPOD_APP_DIR}/third_party/alac
  ${NEUPOD_LVGL_DIR}
  ${CMAKE_BINARY_DIR}/gen
)
# LVGL's pool lives in PSRAM here (main/linker.lf), so it is sized like the hosted builds (192 KB), not the RP2350's 72 KB of internal RAM.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE NEUPOD_LVGL_POOL_BYTES=196608u ${NEUPOD_UI_PROFILE_DEFS} ${NEUPOD_BENCH_DEFS})

# Warning flags for project code only (never for third_party or the SDK), as in app/CMakeLists.txt.
set_source_files_properties(${NEUPOD_PROJECT_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
set_source_files_properties(${NEUPOD_CODEC_VENDOR_SOURCES} ${NEUPOD_APP_DIR}/third_party/tjpgd/tjpgd.c PROPERTIES COMPILE_OPTIONS "-w")
# codec.c sees the ALAC headers, whose FourCC enum constants are multi-character literals.
set_source_files_properties(${NEUPOD_APP_DIR}/audio/codec.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS};-Wno-multichar")

# Generated version header (git hash refreshed on every build), as app/CMakeLists.txt does.
set(NEUPOD_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${NEUPOD_GEN_DIR})
add_custom_target(neupod_version
  COMMAND ${CMAKE_COMMAND} -DOUT=${NEUPOD_GEN_DIR}/neupod_version.h -DSRC_DIR=${NEUPOD_ROOT}
          -DVERSION=0.1.0-dev -P ${NEUPOD_ROOT}/cmake/version.cmake
  BYPRODUCTS ${NEUPOD_GEN_DIR}/neupod_version.h
  COMMENT "Generating neupod_version.h")
add_dependencies(${COMPONENT_LIB} neupod_version)
