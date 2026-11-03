// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>

#include "hal_host.h"
#include "hal_system.h"
#include "host_internal.h"

static struct timespec g_start;

static hal_host_delay_fn g_delay_hook;

void hal_host_set_delay_hook(hal_host_delay_fn fn) { g_delay_hook = fn; }

hal_status_t hal_system_late_init(void) { return HAL_OK; }

bool hal_panic_can_block(void) { return true; }

void hal_panic(uint8_t blink_code, const char *msg) {
    (void)blink_code;
    ssize_t r = write(STDERR_FILENO, msg, strlen(msg));
    (void)r;
    abort();
}

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
    if (g_delay_hook != NULL) {
        g_delay_hook(ms); // inside the RTOS: sleep via the scheduler so other tasks keep running
        return;
    }
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

static uint8_t g_chip_id[8] = {0x4E, 0x45, 0x55, 0x50, 0x4F, 0x44, 0x48, 0x31}; // a fixed host test ID ("NEUPODH1")

bool hal_chip_id(uint8_t id[8]) {
    memcpy(id, g_chip_id, 8);
    return true;
}

void hal_host_set_chip_id(const uint8_t id[8]) { memcpy(g_chip_id, id, 8); }

void hal_random(void *buf, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got = f != NULL ? fread(buf, 1, n, f) : 0;
    if (f != NULL) {
        fclose(f);
    }
    for (uint8_t *p = buf; got < n; got++) { // no /dev/urandom: not secure, host only
        p[got] = (uint8_t)rand();
    }
}

const char *hal_platform_name(void) { return "host"; }
