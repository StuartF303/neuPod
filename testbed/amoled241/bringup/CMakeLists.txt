# SPDX-License-Identifier: MPL-2.0
# Bring-up probe for the Waveshare RP2350-Touch-AMOLED-2.41 (testbed only; not part of any neuPod firmware image).
cmake_minimum_required(VERSION 3.27)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD waveshare_rp2350_touch_amoled_2.41)
set(PICO_PLATFORM rp2350)

include(pico_sdk_import.cmake)

# clk_sys in kHz (150 MHz like the Waveshare demo). -DPROBE_SYS_KHZ=216000 gives a 108 MHz PSRAM clock (and 108 MHz display SCLK).
set(PROBE_SYS_KHZ 150000 CACHE STRING "clk_sys in kHz")

project(amoled241_bringup C CXX ASM)
pico_sdk_init()

set(WS ${CMAKE_CURRENT_LIST_DIR}/third_party/waveshare)

add_executable(amoled241_bringup
    src/main.c
    ${WS}/Config/DEV_Config.c
    ${WS}/QSPI_PIO/qspi_pio.c
    ${WS}/AMOLED/AMOLED_2in41.c
    src/psram.c
    ${WS}/Fonts/font16.c
    ${WS}/Fonts/font24.c
)

target_compile_definitions(amoled241_bringup PRIVATE PROBE_SYS_KHZ=${PROBE_SYS_KHZ})

pico_generate_pio_header(amoled241_bringup ${WS}/QSPI_PIO/qspi.pio)

target_include_directories(amoled241_bringup PRIVATE
    ${WS}/Config ${WS}/QSPI_PIO ${WS}/AMOLED ${WS}/Touch ${WS}/Fonts)

target_link_libraries(amoled241_bringup PRIVATE
    pico_stdlib pico_unique_id
    hardware_pio hardware_dma hardware_pwm hardware_i2c hardware_spi hardware_adc
    hardware_gpio hardware_vreg hardware_flash hardware_sync hardware_exception pico_flash)

# USB CDC and UART0 stdio. The USB reset interface stays on (the SDK default), so `picotool load -f` works.
pico_enable_stdio_usb(amoled241_bringup 1)
pico_enable_stdio_uart(amoled241_bringup 1)

pico_add_extra_outputs(amoled241_bringup)
