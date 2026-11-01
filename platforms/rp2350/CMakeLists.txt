# RP2350 (Pico 2 W) platform. Entered from the top-level CMakeLists.txt after pico_sdk_init().

add_library(hal_rp2350 STATIC
  hal/hal_system.c
)
target_include_directories(hal_rp2350 PUBLIC ${CMAKE_SOURCE_DIR}/hal/include)
target_link_libraries(hal_rp2350 PUBLIC
  pico_stdlib
  pico_cyw43_arch_none   # on-board LED is on the CYW43439
)

add_executable(neupod main.c)
target_link_libraries(neupod neupod_app hal_rp2350)

# stdio over UART0 (GP0 TX / GP1 RX -> Debug Probe UART), not USB
pico_enable_stdio_uart(neupod 1)
pico_enable_stdio_usb(neupod 0)

pico_add_extra_outputs(neupod)
