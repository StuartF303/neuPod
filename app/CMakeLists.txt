include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
# Share the project warning flags with the platform/test directories added after app/.
set(NEUPOD_WARN_FLAGS ${NEUPOD_WARN_FLAGS} PARENT_SCOPE)

# Click-wheel frame decoder: tiny and dependency-free so HALs and tests can link it alone.
add_library(neupod_clickwheel STATIC ${NEUPOD_CLICKWHEEL_SOURCES})
target_include_directories(neupod_clickwheel PUBLIC ${NEUPOD_APP_DIR}/include
                                                    ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_clickwheel PUBLIC c_std_11)

# FatFs (vendored, third-party warnings left as upstream ships them) plus the hal_storage glue.
# Needs a hal_storage implementation only if the final executable actually references it.
add_library(neupod_fatfs STATIC ${NEUPOD_FATFS_SOURCES})
target_include_directories(neupod_fatfs PUBLIC ${NEUPOD_APP_DIR}/third_party/fatfs
                                               ${NEUPOD_APP_DIR}/include
                                               ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_fatfs PUBLIC c_std_11)

# Portable application code. Must not depend on any platform SDK.
add_library(neupod_app STATIC ${NEUPOD_CORE_SOURCES} ${NEUPOD_GFX_SOURCES}
  ${NEUPOD_DEMO_SOURCES})
target_include_directories(neupod_app PUBLIC ${NEUPOD_APP_INCLUDE_DIRS})
target_compile_features(neupod_app PUBLIC c_std_11)
target_link_libraries(neupod_app PUBLIC neupod_clickwheel neupod_fatfs)

# microSD test menu. Deliberately a separate lib that sees only app/include + hal/include (no
# FatFs headers), so RP2350 targets can pair it with the SD library's own FatFs.
add_library(neupod_storage_tests STATIC ${NEUPOD_STORAGE_TEST_SOURCES})
target_include_directories(neupod_storage_tests PUBLIC ${NEUPOD_APP_DIR}/include
                                                       ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_storage_tests PUBLIC c_std_11)

# Secrets at rest (crypto_secret.h): AES-256-GCM + HKDF-SHA256 from the Pico SDK's mbed TLS (Apache-2.0 option of its dual licence).
# Only seven library files are compiled, with our own tiny config (app/crypto/neupod_mbedtls_config.h). On the host the SDK
# may be absent (CI host job): then a clearly named stub is built and the crypto tests are skipped.
if(DEFINED PICO_SDK_PATH)
  set(NEUPOD_MBEDTLS_DIR ${PICO_SDK_PATH}/lib/mbedtls)
else()
  set(NEUPOD_MBEDTLS_DIR $ENV{PICO_SDK_PATH}/lib/mbedtls)
endif()
if(EXISTS ${NEUPOD_MBEDTLS_DIR}/library/gcm.c)
  set(NEUPOD_HAVE_MBEDTLS ON PARENT_SCOPE)
  set(NEUPOD_HAVE_MBEDTLS ON)
  set(NEUPOD_MBEDTLS_FILES aes gcm cipher cipher_wrap sha256 md hkdf platform platform_util constant_time error)
  set(NEUPOD_MBEDTLS_SRC "")
  foreach(F ${NEUPOD_MBEDTLS_FILES})
    list(APPEND NEUPOD_MBEDTLS_SRC ${NEUPOD_MBEDTLS_DIR}/library/${F}.c)
  endforeach()
  add_library(neupod_mbedtls STATIC ${NEUPOD_MBEDTLS_SRC})
  target_include_directories(neupod_mbedtls PUBLIC ${NEUPOD_MBEDTLS_DIR}/include)
  target_include_directories(neupod_mbedtls PRIVATE ${NEUPOD_MBEDTLS_DIR}/library)
  target_compile_definitions(neupod_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="${NEUPOD_APP_DIR}/crypto/neupod_mbedtls_config.h")
  target_compile_options(neupod_mbedtls PRIVATE -w)
  add_library(neupod_crypto STATIC ${NEUPOD_APP_DIR}/crypto/crypto_secret.c ${NEUPOD_APP_DIR}/crypto/key_provider_chipid.c)
  target_include_directories(neupod_crypto PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
  target_link_libraries(neupod_crypto PUBLIC neupod_mbedtls)
  set_source_files_properties(${NEUPOD_APP_DIR}/crypto/crypto_secret.c ${NEUPOD_APP_DIR}/crypto/key_provider_chipid.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
elseif(NEUPOD_REQUIRE_CRYPTO)
  message(FATAL_ERROR "mbed TLS not found at ${NEUPOD_MBEDTLS_DIR} and NEUPOD_REQUIRE_CRYPTO=ON (CI sets it): set PICO_SDK_PATH to a Pico SDK with lib/mbedtls")
else()
  message(WARNING "mbed TLS not found at ${NEUPOD_MBEDTLS_DIR} (set PICO_SDK_PATH): building the crypto STUB; secrets are NOT encrypted in this build")
  add_library(neupod_crypto STATIC ${NEUPOD_APP_DIR}/crypto/crypto_stub.c)
  target_include_directories(neupod_crypto PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
endif()

# Settings store: key/value on the SD card (settings_store.h). Uses nfs (neupod_storage_tests holds nfs_path.c) and hal_storage.
add_library(neupod_settings STATIC ${NEUPOD_SETTINGS_SOURCES})
target_include_directories(neupod_settings PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_settings PUBLIC c_std_11)
target_link_libraries(neupod_settings PUBLIC neupod_storage_tests neupod_crypto)

# Music library index: collation, scanner (FatFs on the card or the host disk image), reader. Needs hal_storage's write lock (hal_host in tests).
add_library(neupod_library STATIC ${NEUPOD_LIBRARY_SOURCES})
target_include_directories(neupod_library PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_library PUBLIC c_std_11)
target_link_libraries(neupod_library PUBLIC neupod_fatfs neupod_rt_core)
set_source_files_properties(${NEUPOD_LIBRARY_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
# The same sources with the power-cut / failing-rename hooks the crash tests need (library.h, NEUPOD_LIB_TEST_HOOKS). Never linked into firmware.
add_library(neupod_library_test STATIC ${NEUPOD_LIBRARY_SOURCES})
target_include_directories(neupod_library_test PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_library_test PUBLIC c_std_11)
target_compile_definitions(neupod_library_test PUBLIC NEUPOD_LIB_TEST_HOOKS=1)
target_link_libraries(neupod_library_test PUBLIC neupod_fatfs neupod_rt_core)

# MAX17048 / DRV2605L drivers: need hal_i2c + hal_system only.
add_library(neupod_drivers STATIC ${NEUPOD_DRIVER_SOURCES})
target_include_directories(neupod_drivers PUBLIC ${NEUPOD_APP_DIR}/include
                                                 ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_drivers PUBLIC c_std_11)

# I2C test menu (needs hal_i2c, hal_battery/haptic via neupod_drivers, hal_display, hal_console).
add_library(neupod_i2c_tests STATIC ${NEUPOD_I2C_TEST_SOURCES})
target_include_directories(neupod_i2c_tests PUBLIC ${NEUPOD_APP_DIR}/include
                                                   ${NEUPOD_APP_DIR}/../hal/include)
target_compile_features(neupod_i2c_tests PUBLIC c_std_11)
target_link_libraries(neupod_i2c_tests PUBLIC neupod_drivers)

set_source_files_properties(${NEUPOD_DRIVER_SOURCES} ${NEUPOD_I2C_TEST_SOURCES} ${NEUPOD_STORAGE_TEST_SOURCES} ${NEUPOD_SETTINGS_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
set_source_files_properties(${NEUPOD_CORE_SOURCES} ${NEUPOD_GFX_SOURCES} ${NEUPOD_DEMO_SOURCES}
  ${NEUPOD_CLICKWHEEL_SOURCES} ${NEUPOD_APP_DIR}/storage/diskio.c
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# --- Runtime: FreeRTOS tasks, LVGL UI, boot checks, audio timing stub ------------------------------
# Logic with no OS dependency (ring buffer, input event decoder) in its own lib so tests link it alone.
add_library(neupod_rt_core STATIC ${NEUPOD_RT_CORE_SOURCES})
target_include_directories(neupod_rt_core PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include PRIVATE ${NEUPOD_APP_DIR}/ui ${NEUPOD_APP_DIR}/third_party/tjpgd)
target_compile_features(neupod_rt_core PUBLIC c_std_11)

# LVGL v9 (submodule lib/lvgl, MIT): every .c under src/ (files for disabled features compile to
# nothing), configured by app/ui/lv_conf.h. Third-party: no project warning flags.
set(NEUPOD_LVGL_DIR ${CMAKE_SOURCE_DIR}/lib/lvgl)
if(NOT EXISTS ${NEUPOD_LVGL_DIR}/src/lvgl.h AND NOT EXISTS ${NEUPOD_LVGL_DIR}/lvgl.h)
  message(FATAL_ERROR "LVGL submodule missing. Run: git submodule update --init lib/lvgl")
endif()
file(GLOB_RECURSE NEUPOD_LVGL_SOURCES CONFIGURE_DEPENDS ${NEUPOD_LVGL_DIR}/src/*.c)
# The UI's own fonts (app/ui/fonts, generated by tools/gen_ui_fonts.sh) ride in this library: they replace LVGL's built-in Montserrat set.
# Small (12 / 16 px) and the Lucide icon sets (16 / 24 / 36 px) are part of the design system (docs/ui.md).
set(NEUPOD_UI_FONT_SOURCES ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_12.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_14.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_20.c
    ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_48.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_icons_16.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_icons_24.c)
if(NEUPOD_UI_HIRES) # 480x360 profile: hosted builds only
  list(APPEND NEUPOD_UI_FONT_SOURCES ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_16.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_font_28.c ${NEUPOD_APP_DIR}/ui/fonts/neupod_icons_36.c)
endif()
add_library(lvgl STATIC ${NEUPOD_LVGL_SOURCES} ${NEUPOD_UI_FONT_SOURCES})
target_include_directories(lvgl PUBLIC ${NEUPOD_LVGL_DIR} ${NEUPOD_APP_DIR}/ui ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/os) # os: memory_budget.h
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_features(lvgl PUBLIC c_std_11)

# Generated version header (git hash refreshed on every build).
set(NEUPOD_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${NEUPOD_GEN_DIR})
add_custom_target(neupod_version
  COMMAND ${CMAKE_COMMAND} -DOUT=${NEUPOD_GEN_DIR}/neupod_version.h -DSRC_DIR=${CMAKE_SOURCE_DIR}
          -DVERSION=0.1.0-dev -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS ${NEUPOD_GEN_DIR}/neupod_version.h
  COMMENT "Generating neupod_version.h")

add_library(neupod_runtime STATIC ${NEUPOD_RUNTIME_SOURCES} ${NEUPOD_AUDIO_CORE_SOURCES} ${NEUPOD_CODEC_VENDOR_SOURCES} ${NEUPOD_UI_SOURCES})
target_include_directories(neupod_runtime PRIVATE ${NEUPOD_APP_DIR}/third_party/alac)
set_source_files_properties(${NEUPOD_CODEC_VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
add_dependencies(neupod_runtime neupod_version)
target_include_directories(neupod_runtime PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include
                                          PRIVATE ${NEUPOD_GEN_DIR})
target_compile_features(neupod_runtime PUBLIC c_std_11)
target_link_libraries(neupod_runtime PUBLIC neupod_rt_core neupod_drivers neupod_settings neupod_freertos lvgl)
# The library's files (FatFs): hosted builds link them with the project's FatFs (neupod_fatfs); the RP2350's FatFs is the SD library's, which
# platforms/rp2350/CMakeLists.txt compiles the library against, so it must not be linked here too.
if(NEUPOD_PLATFORM STREQUAL "host")
  # The simulator's card is a folder (nfs_posix.c), not a FatFs volume: the library is built against FatFs-style calls over POSIX there
  # (app/include/lib_ff_posix.h, hal/host/lib_ff_posix.c). The unit tests use the real FatFs (neupod_library above).
  add_library(neupod_library_posix STATIC ${NEUPOD_LIBRARY_SOURCES})
  target_include_directories(neupod_library_posix PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include)
  target_compile_features(neupod_library_posix PUBLIC c_std_11)
  target_compile_definitions(neupod_library_posix PRIVATE NEUPOD_LIB_POSIX=1)
  target_link_libraries(neupod_library_posix PUBLIC neupod_rt_core)
  set_source_files_properties(${NEUPOD_LIBRARY_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
  target_compile_definitions(neupod_runtime PRIVATE NEUPOD_LIB_POSIX=1)
  target_link_libraries(neupod_runtime PUBLIC neupod_library_posix)
elseif(NOT NEUPOD_PLATFORM STREQUAL "rp2350")
  target_link_libraries(neupod_runtime PUBLIC neupod_library)
endif()
set_source_files_properties(${NEUPOD_RT_CORE_SOURCES} ${NEUPOD_RUNTIME_SOURCES} ${NEUPOD_AUDIO_CORE_SOURCES} ${NEUPOD_UI_SOURCES}
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
set_source_files_properties(${NEUPOD_APP_DIR}/third_party/tjpgd/tjpgd.c PROPERTIES COMPILE_OPTIONS "-w")
# codec.c sees the ALAC headers, whose FourCC enum constants are multi-character literals.
set_source_files_properties(${NEUPOD_APP_DIR}/audio/codec.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS};-Wno-multichar")
