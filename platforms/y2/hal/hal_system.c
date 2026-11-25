// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 system HAL: monotonic time (the device's wall clock is wrong, nothing here depends on it), a chip ID from the
 * eMMC's CID, /dev/urandom, and the "no radio" answer of hal_system_late_init().
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hal_system.h"
#include "y2_hal.h"

static struct timespec g_start;
static void (*g_delay_hook)(uint32_t);

void y2_set_delay_hook(void (*fn)(uint32_t ms)) { g_delay_hook = fn; }

hal_status_t hal_system_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_start);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return HAL_OK;
}

// The radio on the Y2 is the combo chip's Bluetooth through /dev/stpbt (there is no Wi-Fi: hal_wifi_present() is false). The boot check
// treats a failure here as "radio absent" and marks Bluetooth and Wi-Fi unavailable, which is what makes the UI grey them out. OK only if
// the address file and the device are both usable; opening the device powers the chip (about 1.3 s).
hal_status_t hal_system_late_init(void) {
    static bool logged;
    bool ok = y2_bt_probe();
    if (!logged) {
        logged = true;
        printf(ok ? "radio: Bluetooth present on /dev/stpbt (no Wi-Fi on the Y2)\n"
                  : "radio: not present on the Y2 (Wi-Fi and Bluetooth unavailable)\n");
    }
    return ok ? HAL_OK : HAL_ENODEV;
}

// FreeRTOS idle hook (configUSE_IDLE_HOOK, see hal/host/freertos/FreeRTOSConfig.h). The POSIX port's idle task is an
// ordinary thread that would spin at 100% of a core; sleeping here costs nothing in latency because the 1 ms tick
// signal interrupts the sleep and runs the scheduler regardless.
//
// The first call also makes the tick signal restart interrupted system calls (SA_RESTART). The port installs its SIGALRM
// handler without it, so any read/write/pread/pwrite/open that is in progress when a tick lands would fail with EINTR;
// every call site here also retries EINTR itself, this removes the interruptions in the first place. It does not change what
// the port relies on: nanosleep, poll and condition-variable waits are never restarted by SA_RESTART, so the port's own
// sleeps and the idle hook below still return early on a tick, exactly as before.
void vApplicationIdleHook(void) {
    static bool restart_set;
    if (!restart_set) {
        restart_set = true;
        struct sigaction sa;
        if (sigaction(SIGALRM, NULL, &sa) == 0) {
            sa.sa_flags |= SA_RESTART;
            (void)sigaction(SIGALRM, &sa, NULL);
        }
    }
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 5 * 1000 * 1000};
    nanosleep(&ts, NULL);
}

bool hal_panic_can_block(void) { return true; }

void hal_panic(uint8_t blink_code, const char *msg) {
    (void)blink_code;
    ssize_t r = write(STDERR_FILENO, msg, strlen(msg));
    (void)r;
    abort();
}

size_t hal_stack_free_min(void) { return 0; }

uint64_t hal_micros(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)(now.tv_sec - g_start.tv_sec) * 1000000u + (uint64_t)((now.tv_nsec - g_start.tv_nsec) / 1000);
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
    struct timespec ts = {.tv_sec = (time_t)(ms / 1000u), .tv_nsec = (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
}

void hal_status_led(bool on) { (void)on; }

// Not secret (docs/wifi.md): it only binds the secrets file to one unit. The eMMC CID is stable per device; it is
// read from the MMC host's sysfs attribute, never from the block device.
bool hal_chip_id(uint8_t id[8]) {
    static const char *const k_cid = "/sys/class/mmc_host/mmc0/mmc0:0001/cid";
    char hex[64] = {0};
    FILE *f = fopen(k_cid, "r");
    if (f == NULL || fgets(hex, sizeof hex, f) == NULL) {
        if (f != NULL) {
            fclose(f);
        }
        return false;
    }
    fclose(f);
    uint64_t h = 1469598103934665603ull; // FNV-1a over the hex text
    for (const char *p = hex; *p != '\0' && *p != '\n'; p++) {
        h = (h ^ (uint8_t)*p) * 1099511628211ull;
    }
    for (int i = 0; i < 8; i++) {
        id[i] = (uint8_t)(h >> (8 * i));
    }
    return true;
}

void hal_random(void *buf, size_t n) {
    int fd;
    do {
        fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    size_t got = 0;
    if (fd >= 0) {
        while (got < n) {
            ssize_t r = read(fd, (uint8_t *)buf + got, n - got);
            if (r < 0 && errno == EINTR) {
                continue; // the 1 kHz tick signal: not an error
            }
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        close(fd);
    }
    if (got < n) { // no /dev/urandom at all: fail loudly rather than hand out predictable "random" bytes
        hal_panic(0, "hal_random: /dev/urandom unavailable\n");
    }
}

const char *hal_platform_name(void) { return "y2"; }

// Power-off request from the UI (a 10 s Power hold, app/ui/ui_hold.c): main.c installs the handler that starts the orderly stop and
// the system poweroff (exit status 77 keeps the init wrapper from respawning the firmware). Without a handler the request is refused.
static void (*g_poweroff_fn)(void);

void y2_input_set_power_handler(void (*fn)(void)) { g_poweroff_fn = fn; }

bool hal_system_can_poweroff(void) { return g_poweroff_fn != NULL; }

bool hal_system_poweroff(void) {
    if (g_poweroff_fn == NULL) {
        return false;
    }
    printf("y2: power held, shutting down\n");
    g_poweroff_fn();
    return true;
}
