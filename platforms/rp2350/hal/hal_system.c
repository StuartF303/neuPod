// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

// clk_sys target. 125 MHz (not the SDK's 150 MHz default) so the display SPI, which can only run
// at clk_peri / 2 (or slower even divisions), hits exactly 62.5 MHz; 150 MHz only gives 75/37.5.
#define NEUPOD_SYS_CLOCK_KHZ 125000u

// Stack watermark: the unused part of core 0's stack is painted at start-up (from the linker's
// __StackBottom up to just below the current stack pointer) and measured on request.
extern uint32_t __StackBottom[]; // linker symbol: lowest address of core 0's stack
#define STACK_PAINT 0xA5A5A5A5u

static void paint_stack(void) {
    uint32_t *p = __StackBottom;
    uint32_t *sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    for (; p < sp - 16; p++) { // stay clear of the live frames
        *p = STACK_PAINT;
    }
}

size_t hal_stack_free_min(void) {
    const uint32_t *p = __StackBottom;
    size_t words = 0;
    while (*p == STACK_PAINT) {
        p++;
        words++;
    }
    return words * sizeof(uint32_t);
}

hal_status_t hal_system_init(void) {
    paint_stack();
    // Must come BEFORE stdio_init_all(): the UART divisor is computed from clk_peri at init.
    set_sys_clock_khz(NEUPOD_SYS_CLOCK_KHZ, true);
    // set_sys_clock_khz() re-sources clk_peri from the 48 MHz USB PLL (unless
    // PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK is set); put it back on clk_sys so UART and
    // SPI run from 125 MHz.
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                              NEUPOD_SYS_CLOCK_KHZ * 1000u);
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("neuPod: cyw43_arch_init failed\n");
        return HAL_ENODEV;
    }
    return HAL_OK;
}

uint64_t hal_micros(void) { return to_us_since_boot(get_absolute_time()); }

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void hal_delay_ms(uint32_t ms) { sleep_ms(ms); }

void hal_status_led(bool on) { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on); }

const char *hal_platform_name(void) { return "rp2350"; }
