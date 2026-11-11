// SPDX-License-Identifier: MPL-2.0
// Charging-only mode: no UI, storage, audio or FreeRTOS. A 250 ms sleep loop reading the side-key node (Power) and the
// charger supplies; the decisions are in hal/y2_charge.c (unit-tested).
#define _POSIX_C_SOURCE 200809L
#include "charging.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "y2_charge.h"
#include "y2_hal.h"
#include "y2_poweroff.h"

static volatile sig_atomic_t g_term;
static void on_term(int sig) {
    (void)sig;
    g_term = 1;
}

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void backlight_off(void) {
    int fd = open(Y2_BACKLIGHT, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("charging: no backlight control at %s: %s\n", Y2_BACKLIGHT, strerror(errno));
        return;
    }
    if (write(fd, "0\n", 2) != 2) {
        printf("charging: backlight write failed: %s\n", strerror(errno));
    }
    close(fd);
}

// Black frame as well, so nothing stale (the boot splash, a previous run) is visible if the backlight comes back on.
static void framebuffer_black(void) {
    int fd = open(Y2_FB_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("charging: cannot open %s: %s\n", Y2_FB_DEVICE, strerror(errno));
        return;
    }
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0) {
        printf("charging: FBIOGET_*SCREENINFO failed: %s\n", strerror(errno));
    } else {
        void *map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map != MAP_FAILED) {
            memset(map, 0, fix.smem_len);
            munmap(map, fix.smem_len);
            var.xoffset = 0;
            var.yoffset = 0;
            (void)ioctl(fd, FBIOPAN_DISPLAY, &var); // a write is only guaranteed to reach the panel after a pan
        } else {
            printf("charging: framebuffer mmap failed: %s\n", strerror(errno));
        }
    }
    close(fd);
}

static void drain(int fd, y2_charge_t *c) {
    uint8_t buf[Y2_EVENT_SIZE * 32];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            return; // EAGAIN: empty (a dead node just yields no more events; poweroff on unplug still works)
        }
        y2_event_t ev[32];
        size_t used = 0;
        size_t cnt = y2_evdev_decode(buf, (size_t)r, ev, 32, &used);
        uint32_t now = now_ms();
        for (size_t i = 0; i < cnt; i++) {
            y2_charge_feed(c, &ev[i], now);
        }
        if ((size_t)r < sizeof buf) {
            return;
        }
    }
}

int y2_charging_main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; // no SA_RESTART: a signal ends the sleep at once
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    backlight_off();
    framebuffer_black();

    // Only mtk-kpd (the side keys, including Power) is needed; the wheel node is left alone.
    int fd = -1;
    char path[64];
    if (y2_input_find_node(Y2_INPUT_CLASS, Y2_DEV_INPUT, "mtk-kpd", path, sizeof path)) {
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            printf("charging: cannot open %s: %s\n", path, strerror(errno));
        }
    } else {
        printf("charging: no evdev node named mtk-kpd; Power key unavailable\n");
    }
    printf("charging: charging-only mode (no UI), Power held 2 s reboots, unplug for 3 s powers off\n");
    fflush(stdout);

    y2_charge_t c;
    y2_charge_init(&c);
    int rc = 0;
    while (!g_term) {
        if (fd >= 0) {
            drain(fd, &c);
        }
        uint32_t now = now_ms();
        y2_charge_action_t act = y2_charge_step(&c, now, y2_charge_read_online(Y2_POWER_SUPPLY));
        if (act == Y2_CHARGE_REBOOT) {
            printf("charging: power held, requesting reboot (%s)\n", y2_reboot_command());
            fflush(NULL);
            if (y2_reboot_spawn(NULL)) {
                rc = Y2_EXIT_REQUESTED;
                break;
            }
            printf("charging: cannot start %s, staying up\n", y2_reboot_command());
            y2_charge_failed(&c, act, now);
        } else if (act == Y2_CHARGE_POWEROFF) {
            printf("charging: charger removed for %u ms, requesting poweroff (%s)\n", Y2_CHARGE_UNPLUG_MS, y2_poweroff_command());
            fflush(NULL);
            if (y2_poweroff_spawn(NULL)) {
                rc = Y2_EXIT_REQUESTED;
                break;
            }
            printf("charging: cannot start %s, staying up\n", y2_poweroff_command());
            y2_charge_failed(&c, act, now);
        }
        fflush(stdout);
        struct timespec ts = {.tv_sec = 0, .tv_nsec = (long)Y2_CHARGE_POLL_MS * 1000000L};
        nanosleep(&ts, NULL); // EINTR (SIGTERM) just ends the wait early
    }
    if (fd >= 0) {
        close(fd);
    }
    printf("charging: exiting (%d)\n", rc);
    fflush(stdout);
    return rc;
}
