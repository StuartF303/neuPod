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

set_source_files_properties(${NEUPOD_DRIVER_SOURCES} ${NEUPOD_I2C_TEST_SOURCES} ${NEUPOD_STORAGE_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
set_source_files_properties(${NEUPOD_CORE_SOURCES} ${NEUPOD_GFX_SOURCES} ${NEUPOD_DEMO_SOURCES}
  ${NEUPOD_CLICKWHEEL_SOURCES} ${NEUPOD_APP_DIR}/storage/diskio.c
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# --- Runtime: FreeRTOS tasks, LVGL UI, boot checks, audio timing stub ------------------------------
# Logic with no OS dependency (ring buffer, input event decoder) in its own lib so tests link it alone.
add_library(neupod_rt_core STATIC ${NEUPOD_RT_CORE_SOURCES})
target_include_directories(neupod_rt_core PUBLIC ${NEUPOD_APP_DIR}/include ${NEUPOD_APP_DIR}/../hal/include PRIVATE ${NEUPOD_APP_DIR}/ui)
target_compile_features(neupod_rt_core PUBLIC c_std_11)

# LVGL v9 (submodule lib/lvgl, MIT): every .c under src/ (files for disabled features compile to
# nothing), configured by app/ui/lv_conf.h. Third-party: no project warning flags.
set(NEUPOD_LVGL_DIR ${CMAKE_SOURCE_DIR}/lib/lvgl)
if(NOT EXISTS ${NEUPOD_LVGL_DIR}/src/lvgl.h AND NOT EXISTS ${NEUPOD_LVGL_DIR}/lvgl.h)
  message(FATAL_ERROR "LVGL submodule missing. Run: git submodule update --init lib/lvgl")
endif()
file(GLOB_RECURSE NEUPOD_LVGL_SOURCES CONFIGURE_DEPENDS ${NEUPOD_LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${NEUPOD_LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${NEUPOD_LVGL_DIR} ${NEUPOD_APP_DIR}/ui ${NEUPOD_APP_DIR}/include)
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
target_link_libraries(neupod_runtime PUBLIC neupod_rt_core neupod_drivers neupod_freertos lvgl)
set_source_files_properties(${NEUPOD_RT_CORE_SOURCES} ${NEUPOD_RUNTIME_SOURCES} ${NEUPOD_AUDIO_CORE_SOURCES} ${NEUPOD_UI_SOURCES}
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
# codec.c sees the ALAC headers, whose FourCC enum constants are multi-character literals.
set_source_files_properties(${NEUPOD_APP_DIR}/audio/codec.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS};-Wno-multichar")
