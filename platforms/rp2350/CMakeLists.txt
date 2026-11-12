# RP2350 (Pico 2 W) platform. Entered from the top-level CMakeLists.txt after pico_sdk_init().

# HAL in three layers. hal_rp2350_core holds everything that does not care about the scheduler.
# hal_rp2350 (bare metal: the display/storage/i2c test executables) and hal_rp2350_rtos (the
# FreeRTOS runtime) differ in hal_system.c (cyw43 arch, deferred radio bring-up) and the audio sink.
# Audio sink: -DNEUPOD_AUDIO_SINK=i2s (default, PIO + DMA to the Pico Audio Pack) or null (discards samples
# on a real-time clock; no pins touched).
set(NEUPOD_AUDIO_SINK i2s CACHE STRING "Audio sink for the RP2350 build: i2s or null")
set_property(CACHE NEUPOD_AUDIO_SINK PROPERTY STRINGS i2s null)
if(NEUPOD_AUDIO_SINK STREQUAL "i2s")
  set(NEUPOD_AUDIO_SINK_SRC hal/hal_audio.c)
elseif(NEUPOD_AUDIO_SINK STREQUAL "null")
  set(NEUPOD_AUDIO_SINK_SRC hal/hal_audio_null.c)
else()
  message(FATAL_ERROR "NEUPOD_AUDIO_SINK must be i2s or null (got '${NEUPOD_AUDIO_SINK}')")
endif()
message(STATUS "neuPod audio sink: ${NEUPOD_AUDIO_SINK}")

# GP22 (Audio Pack mute / amp enable) level that means UNMUTED. TODO(hw): polarity unconfirmed.
set(NEUPOD_AUDIO_UNMUTE_LEVEL 1 CACHE STRING "GPIO level on GP22 that unmutes the Audio Pack (0 or 1)")
set_property(CACHE NEUPOD_AUDIO_UNMUTE_LEVEL PROPERTY STRINGS 0 1)
add_compile_definitions(NEUPOD_AUDIO_UNMUTE_LEVEL=${NEUPOD_AUDIO_UNMUTE_LEVEL}u)
message(STATUS "neuPod audio unmute level on GP22: ${NEUPOD_AUDIO_UNMUTE_LEVEL}")

add_library(hal_rp2350_core STATIC
  hal/hal_display.c
  hal/hal_console.c
  hal/hal_input.c
  hal/hal_i2c.c
  ${NEUPOD_AUDIO_SINK_SRC}   # core-1 code: no RTOS dependency (ADR-0011), shared by both builds
  hal/hal_core.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
)
target_include_directories(hal_rp2350_core PUBLIC ${CMAKE_SOURCE_DIR}/hal/include)
target_link_libraries(hal_rp2350_core PUBLIC
  neupod_clickwheel
  pico_stdlib
  hardware_spi
  hardware_dma
  hardware_pwm
  hardware_pio
  hardware_clocks
  hardware_i2c
  pico_multicore
  pico_rand
  pico_unique_id
)
set_source_files_properties(hal/hal_core.c hal/hal_i2c.c i2c_test_main.c ${CMAKE_SOURCE_DIR}/hal/common/hal_i2c_common.c
  hal/hal_system.c hal/bt_port_cyw43.c hal/hal_wifi.c hal/hal_display.c hal/hal_console.c hal/hal_input.c hal/hal_audio.c hal/hal_audio_null.c main.c
  ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack/bt_btstack.c
  display_test_main.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
pico_generate_pio_header(hal_rp2350_core ${CMAKE_CURRENT_LIST_DIR}/hal/clickwheel_rx.pio)
if(NEUPOD_AUDIO_SINK STREQUAL "i2s")
  pico_generate_pio_header(hal_rp2350_core ${CMAKE_CURRENT_LIST_DIR}/hal/neupod_i2s.pio)
endif()

# Bare metal: the SDK's threadsafe-background cyw43 arch (the LED lives on the CYW43439).
add_library(hal_rp2350 STATIC hal/hal_system.c)
target_link_libraries(hal_rp2350 PUBLIC hal_rp2350_core pico_cyw43_arch_none)

# FreeRTOS (single-core, core 0): pico_cyw43_arch_sys_freertos (via neupod_freertos). Radio bring-up
# waits for the scheduler (hal_system_late_init).
add_library(hal_rp2350_rtos STATIC hal/hal_system.c hal/bt_port_cyw43.c hal/hal_wifi.c
  ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack/bt_btstack.c)
target_compile_definitions(hal_rp2350_rtos PRIVATE NEUPOD_RTOS=1)
target_link_libraries(hal_rp2350_rtos PUBLIC hal_rp2350_core neupod_freertos)

# Bluetooth classic: BTstack from the Pico SDK (lib/btstack, v1.6.2) on the CYW43 async context. Deliberately NOT
# pico_btstack_cyw43 (it pulls pico_btstack_flash_bank / pico_flash: run-time flash writes, which stall core 1, see
# docs/bluetooth.md); bt_port_cyw43.c supplies btstack_cyw43_init() instead; the link logic is hal/common/bt_btstack/ (link keys: the SD-backed settings store).
# pico_btstack_sbc_encoder is linked now to prove it builds; nothing references it until stage 2, so it adds no flash.
if(NOT TARGET pico_btstack_classic)
  message(FATAL_ERROR "BTstack is missing from the Pico SDK. Run: git -C $ENV{PICO_SDK_PATH} submodule update --init lib/btstack")
endif()
target_include_directories(hal_rp2350_rtos PUBLIC ${CMAKE_CURRENT_LIST_DIR}/btstack)
target_include_directories(hal_rp2350_rtos PRIVATE ${CMAKE_SOURCE_DIR}/hal/common/bt_btstack)
target_link_libraries(hal_rp2350_rtos PUBLIC
  pico_btstack_classic
  pico_btstack_hci_transport_cyw43
  pico_btstack_run_loop_async_context
  pico_btstack_sbc_encoder
  pico_lwip_sntp
  pico_lwip_http
)

add_executable(neupod main.c)
target_link_libraries(neupod neupod_runtime neupod_drivers hal_rp2350_rtos hal_rp2350_storage_rtos)

# stdio over UART0 (GP0 TX / GP1 RX -> Debug Probe UART), not USB
pico_enable_stdio_uart(neupod 1)
pico_enable_stdio_usb(neupod 0)

pico_add_extra_outputs(neupod)

# Core-1 rules on the linked image (ADR-0011): nothing reachable from the audio core may touch the RTOS, stdio, the heap
# or the panic path. The vendored decoders make a source scan inadequate, so this walks the call graph of the ELF.
# Python is required: the check is part of the build, a missing interpreter must not silently skip it.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET neupod POST_BUILD
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_core1_closure.py ${CMAKE_OBJDUMP} $<TARGET_FILE:neupod>
  COMMENT "Checking the audio core's call closure (core-1 rules)")

# Interactive display test menu over UART for the ST7789 (flash with: scripts/flash.sh rp2350 display_test)
add_executable(display_test display_test_main.c)
target_link_libraries(display_test neupod_app hal_rp2350)
pico_enable_stdio_uart(display_test 1)
pico_enable_stdio_usb(display_test 0)
pico_add_extra_outputs(display_test)

# Interactive I2C test menu over UART (fuel gauge + haptic driver; flash with: scripts/flash.sh rp2350 i2c_test)
add_executable(i2c_test i2c_test_main.c)
target_link_libraries(i2c_test neupod_i2c_tests hal_rp2350)
pico_enable_stdio_uart(i2c_test 1)
pico_enable_stdio_usb(i2c_test 0)
pico_add_extra_outputs(i2c_test)

# --- microSD (SDIO over PIO) ---------------------------------------------------------------------
# carlk3's no-OS-FatFS-SD-SDIO-SPI-RPi-Pico (Apache-2.0), a git submodule pinned to a release tag.
# It brings its own FatFs and diskio glue, so storage targets must NOT link neupod_fatfs /
# app/storage/diskio.c (duplicate ff.c / disk_* symbols); that is why the storage test menu lives
# in neupod_storage_tests, which has no FatFs headers.
set(NEUPOD_SD_LIB_DIR ${CMAKE_SOURCE_DIR}/lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
if(NOT EXISTS ${NEUPOD_SD_LIB_DIR}/src/CMakeLists.txt)
  message(FATAL_ERROR "SD library submodule missing. Run: git submodule update --init --recursive")
endif()

# FatFs configuration: the library's default ffconf.h is replaced (not edited) by ours, put first in
# its include path as the library's README describes. It must match the host's card policy file.
file(READ ${CMAKE_CURRENT_LIST_DIR}/ffconf/ffconf.h NEUPOD_FFCONF_RP2350)
file(READ ${CMAKE_SOURCE_DIR}/app/third_party/fatfs/ffconf.h NEUPOD_FFCONF_HOST)
if(NOT NEUPOD_FFCONF_RP2350 STREQUAL NEUPOD_FFCONF_HOST)
  message(FATAL_ERROR "platforms/rp2350/ffconf/ffconf.h differs from app/third_party/fatfs/ffconf.h; keep them identical")
endif()
# hal_storage.c assumes 4 PIO cycles per SD clock (the library's CLKDIV).
file(READ ${NEUPOD_SD_LIB_DIR}/src/sd_driver/SDIO/rp2040_sdio.pio NEUPOD_SDIO_PIO_SRC)
if(NOT NEUPOD_SDIO_PIO_SRC MATCHES "\\.define PUBLIC CLKDIV D0 \\+ 1 \\+ D1 \\+ 1" OR
   NOT NEUPOD_SDIO_PIO_SRC MATCHES "\\.define D0 1\n\\.define D1 1")
  message(FATAL_ERROR "SD library CLKDIV changed; update SDIO_PIO_CYCLES_PER_CLK in hal_storage.c")
endif()

add_subdirectory(${NEUPOD_SD_LIB_DIR}/src ${CMAKE_BINARY_DIR}/sd_lib)
target_include_directories(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico BEFORE INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/ffconf)
# Swap the library's ffsystem.c (malloc via the panicking SDK wrapper, whole-cluster requests) for
# app/storage/ff_alloc.c; the library's source list is edited here so lib/ stays untouched.
get_target_property(NEUPOD_SD_SOURCES no-OS-FatFS-SD-SDIO-SPI-RPi-Pico INTERFACE_SOURCES)
list(FILTER NEUPOD_SD_SOURCES EXCLUDE REGEX "/ffsystem\\.c$")
list(APPEND NEUPOD_SD_SOURCES ${CMAKE_SOURCE_DIR}/app/storage/ff_alloc.c)
set_property(TARGET no-OS-FatFS-SD-SDIO-SPI-RPi-Pico PROPERTY INTERFACE_SOURCES ${NEUPOD_SD_SOURCES})

set(NEUPOD_LIBRARY_SRC
  ${CMAKE_SOURCE_DIR}/app/library/lib_collate.c
  ${CMAKE_SOURCE_DIR}/app/library/lib_store.c
  ${CMAKE_SOURCE_DIR}/app/library/lib_esort.c
  ${CMAKE_SOURCE_DIR}/app/library/lib_build.c
  ${CMAKE_SOURCE_DIR}/app/library/lib_reader.c)
function(neupod_add_storage_lib name)
  add_library(${name} STATIC
    hal/hal_storage.c
    hal/hw_config.c
    ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c
    ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c   # portable file API on the library's FatFs
    ${NEUPOD_LIBRARY_SRC}                          # music library index (ADR-0017): FatFs through the library's ff.h; linked only when used
  )
  target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/hal/include
                                            ${CMAKE_SOURCE_DIR}/app/include
                                     PRIVATE ${CMAKE_SOURCE_DIR}/hal/common)
  target_link_libraries(${name} PUBLIC no-OS-FatFS-SD-SDIO-SPI-RPi-Pico pico_stdlib ${ARGN})
  # Library error messages (bus errors, timeouts) go to the UART console.
  target_compile_definitions(${name} PUBLIC USE_PRINTF)
endfunction()
neupod_add_storage_lib(hal_rp2350_storage)
# Same sources for the runtime, built against the SDK's FreeRTOS adapter: the SD library embeds
# pico_sync mutexes in its structs, whose layout changes with the adapter, so one copy cannot serve both.
neupod_add_storage_lib(hal_rp2350_storage_rtos neupod_freertos)
set_source_files_properties(hal/hal_storage.c hal/hw_config.c storage_test_main.c
  ${CMAKE_SOURCE_DIR}/hal/common/hal_storage_common.c ${CMAKE_SOURCE_DIR}/app/storage/nfs_fatfs.c ${NEUPOD_LIBRARY_SRC}
  PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")

# microSD test menu (flash with: scripts/flash.sh rp2350 storage_test)
add_executable(storage_test storage_test_main.c)
target_link_libraries(storage_test neupod_storage_tests hal_rp2350 hal_rp2350_storage)
# 4 KB main stack (the default is 2 KB). That is the maximum: core 0's stack lives in the 4 KB
# SCRATCH_Y bank (0x4000 does not link: "region SCRATCH_Y overflowed"). FatFs (LFN buffers are
# on the heap) plus the test menu are shallow enough; the stack high-water mark is printed after
# every test (hal_stack_free_min) so any margin problem shows up on the bench.
target_compile_definitions(storage_test PRIVATE PICO_STACK_SIZE=0x1000)
pico_enable_stdio_uart(storage_test 1)
pico_enable_stdio_usb(storage_test 0)
pico_add_extra_outputs(storage_test)

# The crypto library needs hal_chip_id() / hal_random().
target_link_libraries(neupod_crypto PUBLIC hal_rp2350_rtos)
