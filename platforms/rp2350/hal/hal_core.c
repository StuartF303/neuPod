// SPDX-License-Identifier: MPL-2.0
#include "hal_core.h"
#include "pico/multicore.h"

// Core 1's stack is painted at start and measured on request (the same trick as hal_stack_free_min
// for core 0). Linker symbols: lowest and highest address of core 1's stack.
extern uint32_t __StackOneBottom[];
extern uint32_t __StackOneTop[];
#define STACK_PAINT 0xA5A5A5A5u

static void (*g_entry)(void);

static void core1_start(void) {
    uint32_t *sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t *p = __StackOneBottom; p < sp - 16; p++) { // stay clear of the live frames
        *p = STACK_PAINT;
    }
    g_entry();
}

size_t hal_core1_stack_free_min(void) {
    const uint32_t *p = __StackOneBottom;
    size_t words = 0;
    while (p < __StackOneTop && *p == STACK_PAINT) {
        p++;
        words++;
    }
    return words * sizeof(uint32_t);
}

hal_status_t hal_core1_launch(void (*entry)(void)) {
    if (entry == NULL) {
        return HAL_EINVAL;
    }
    g_entry = entry;
    // Core 1 shares core 0's vector table (the SDK points it there), so interrupt handlers the
    // audio core installs land in the same RAM table the RTOS port uses; they use different slots.
    multicore_launch_core1(core1_start);
    return HAL_OK;
}
