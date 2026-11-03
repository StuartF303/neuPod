// SPDX-License-Identifier: MPL-2.0
#include "hal_core.h"
#include "pico/multicore.h"

// Core 1's stack is painted at start and measured on request (the same trick as hal_stack_free_min for core 0).
// It is our own array, not the SDK's 2 KB scratch bank: the MP3 decoder alone keeps about 10 KB of scratch on the
// stack (minimp3), and FLAC / ALAC need a few KB. Override with -DNEUPOD_CORE1_STACK_BYTES=n.
#ifndef NEUPOD_CORE1_STACK_BYTES
#define NEUPOD_CORE1_STACK_BYTES 16384u
#endif
static uint32_t g_core1_stack[NEUPOD_CORE1_STACK_BYTES / 4u] __attribute__((aligned(8)));
#define __StackOneBottom g_core1_stack
#define __StackOneTop (g_core1_stack + NEUPOD_CORE1_STACK_BYTES / 4u)
#define STACK_PAINT 0xA5A5A5A5u

static void (*g_entry)(void);

static void core1_start(void) {
    // Stack limit (Cortex-M33 MSPLIM): leave 256 bytes of guard above the array's bottom; running into it raises a fault
    // instead of silently overwriting the RAM below (the decoders keep several KB on this stack).
    __asm volatile("msr msplim, %0" ::"r"(g_core1_stack + 64) : "memory");
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
    multicore_launch_core1_with_stack(core1_start, g_core1_stack, sizeof g_core1_stack);
    return HAL_OK;
}
