add_executable(neupod main.c)

target_link_libraries(neupod
    pico_stdlib
    pico_cyw43_arch_none   # on-board LED is on the CYW43439
    hal_rp2350
)

# stdio over UART0 (GP0 TX / GP1 RX -> Debug Probe UART), not USB
pico_enable_stdio_uart(neupod 1)
pico_enable_stdio_usb(neupod 0)

pico_add_extra_outputs(neupod)
