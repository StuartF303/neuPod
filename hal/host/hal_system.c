// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>

#include "hal_system.h"
#include "host_internal.h"

static struct timespec g_start;

hal_status_t hal_system_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return HAL_OK;
}

size_t hal_stack_free_min(void) { return 0; }

uint64_t hal_micros(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec - g_start.tv_sec) * 1000000u +
           (uint64_t)((now.tv_nsec - g_start.tv_nsec) / 1000);
}

uint32_t hal_millis(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((now.tv_sec - g_start.tv_sec) * 1000 + (now.tv_nsec - g_start.tv_nsec) / 1000000);
}

void hal_delay_ms(uint32_t ms) {
    // Sleep in short slices so the simulator window keeps pumping events (and a close request
    // exits promptly) even during long delays.
    uint32_t start = hal_millis();
    for (;;) {
        host_pump_events();
        uint32_t elapsed = hal_millis() - start;
        if (elapsed >= ms) {
            return;
        }
        uint32_t slice = ms - elapsed < 10 ? ms - elapsed : 10;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)slice * 1000000L};
        nanosleep(&ts, NULL);
    }
}

void hal_status_led(bool on) { (void)on; }

const char *hal_platform_name(void) { return "host"; }
