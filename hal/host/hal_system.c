// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <time.h>

#include "hal_system.h"

static struct timespec g_start;

hal_status_t hal_system_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return HAL_OK;
}

uint32_t hal_millis(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((now.tv_sec - g_start.tv_sec) * 1000 + (now.tv_nsec - g_start.tv_nsec) / 1000000);
}

void hal_delay_ms(uint32_t ms) {
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void hal_status_led(bool on) { (void)on; }

const char *hal_platform_name(void) { return "host"; }
