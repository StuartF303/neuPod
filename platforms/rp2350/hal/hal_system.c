// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "hal_audio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#ifndef NEUPOD_RTOS
#define NEUPOD_RTOS 0
#endif

// NEUPOD_RTOS (set for the FreeRTOS builds): the CYW43 driver runs a worker task, so it can only
// be brought up once the scheduler is running (hal_system_late_init); bare-metal builds do it here.
static volatile bool g_radio_up;

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
    hal_audio_force_mute(); // the Audio Pack's amp is muted from the first moments, before core 1 exists
#if !NEUPOD_RTOS
    return hal_system_late_init();
#else
    return HAL_OK;
#endif
}

hal_status_t hal_system_late_init(void) {
    if (g_radio_up) {
        return HAL_OK;
    }
    if (cyw43_arch_init()) {
        printf("neuPod: cyw43_arch_init failed\n");
        return HAL_ENODEV;
    }
    g_radio_up = true;
    return HAL_OK;
}

// Straight from the timer registers (RAM, no SDK call): also used by the audio core.
uint64_t __not_in_flash_func(hal_micros)(void) {
    uint32_t hi, lo, hi2;
    do {
        hi = timer_hw->timerawh;
        lo = timer_hw->timerawl;
        hi2 = timer_hw->timerawh;
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void hal_delay_ms(uint32_t ms) { sleep_ms(ms); }

void hal_status_led(bool on) {
    if (g_radio_up) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    }
}

// --- Fatal-error path -----------------------------------------------------------------------

// Optional board LED wired straight to a GPIO (-DNEUPOD_PANIC_LED_GPIO=n). The Pico 2 W's only LED
// sits behind the CYW43 chip, whose driver needs a running scheduler, so on that board the LED
// code is shown only when the panic happens in ordinary task context (see hal_panic below).
static void panic_puts(const char *s) {
    uart_inst_t *u = uart_get_instance(PICO_DEFAULT_UART);
    for (; *s != '\0'; s++) {
        if (*s == '\n') {
            uart_putc_raw(u, '\r');
        }
        uart_putc_raw(u, *s);
    }
}

// True when the kernel (or an interrupt) has interrupts masked, i.e. blocking calls cannot work.
static bool kernel_context(void) {
    uint32_t primask, basepri;
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("mrs %0, basepri" : "=r"(basepri));
    return __get_current_exception() != 0 || primask != 0 || basepri != 0;
}

bool hal_panic_can_block(void) { return !kernel_context(); }

void hal_panic(uint8_t blink_code, const char *msg) {
    // Stop the audio core first (core 0 only): a half-dead system must not keep clocking out audio.
    // Resetting core 1 is a pure hardware action (no kernel, no locks), so it works from any context.
    if (get_core_num() == 0) {
        multicore_reset_core1();
    }
    // Core 1's reset does not stop the DMA and PIO it started: mute the amp and halt them (register writes only).
    hal_audio_force_mute();
    panic_puts(msg);
    bool spin_only = kernel_context();
    (void)spin_only; // unused when the LED is a plain GPIO
#ifdef NEUPOD_PANIC_LED_GPIO
    gpio_init(NEUPOD_PANIC_LED_GPIO);
    gpio_set_dir(NEUPOD_PANIC_LED_GPIO, GPIO_OUT);
    spin_only = false;
#endif
    for (unsigned cycle = 0;; cycle++) {
        for (uint8_t i = 0; i < blink_code; i++) {
            for (int on = 1; on >= 0; on--) {
#ifdef NEUPOD_PANIC_LED_GPIO
                gpio_put(NEUPOD_PANIC_LED_GPIO, on);
                busy_wait_ms(200);
#else
                if (!spin_only) {
                    hal_status_led(on != 0);
                    sleep_ms(200);
                } else {
                    busy_wait_ms(200);
                }
#endif
            }
        }
#if defined(NEUPOD_PANIC_LED_GPIO)
        busy_wait_ms(1200);
#else
        if (spin_only) {
            busy_wait_ms(1200);
        } else {
            sleep_ms(1200);
        }
#endif
        if (cycle % 3 == 2) {
            panic_puts(msg);
        }
    }
}

const char *hal_platform_name(void) { return "rp2350"; }
