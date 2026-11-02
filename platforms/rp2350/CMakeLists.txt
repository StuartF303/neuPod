# RP2350 (Pico 2 W) platform. Entered from the top-level CMakeLists.txt after pico_sdk_init().

add_library(hal_rp2350 STATIC
  hal/hal_system.c
  hal/hal_display.c
  hal/hal_console.c
  hal/hal_input.c
)
target_include_directories(hal_rp2350 PUBLIC ${CMAKE_SOURCE_DIR}/hal/include)
target_link_libraries(hal_rp2350 PUBLIC
  neupod_clickwheel
  pico_stdlib
  pico_cyw43_arch_none   # on-board LED is on the CYW43439
  hardware_spi
  hardware_dma
  hardware_pwm
  hardware_pio
  hardware_clocks
)
set_source_files_properties(hal/hal_system.c hal/hal_display.c hal/hal_console.c hal/hal_input.c main.c
  display_test_main.c PROPERTIES COMPILE_OPTIONS "${NEUPOD_WARN_FLAGS}")
pico_generate_pio_header(hal_rp2350 ${CMAKE_CURRENT_LIST_DIR}/hal/clickwheel_rx.pio)

add_executable(neupod main.c)
target_link_libraries(neupod neupod_app hal_rp2350)

# stdio over UART0 (GP0 TX / GP1 RX -> Debug Probe UART), not USB
pico_enable_stdio_uart(neupod 1)
pico_enable_stdio_usb(neupod 0)

pico_add_extra_outputs(neupod)

# Interactive display test menu over UART for the ST7789 (flash with: scripts/flash.sh rp2350 display_test)
add_executable(display_test display_test_main.c)
target_link_libraries(display_test neupod_app hal_rp2350)
pico_enable_stdio_uart(display_test 1)
pico_enable_stdio_usb(display_test 0)
pico_add_extra_outputs(display_test)
