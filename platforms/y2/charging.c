// SPDX-License-Identifier: MPL-2.0
// Charging-only mode: no UI, storage, audio or FreeRTOS. A 250 ms sleep loop reading the side-key node (Power) and the
// charger supplies; the decisions are in hal/y2_charge.c (unit-tested).
#define _POSIX_C_SOURCE 200809L
#include "charging.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "y2_charge.h"
#include "y2_chargeview.h"
#include "y2_fb.h"
#include "y2_power.h"
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

// Backlight: the fd stays open for the life of the mode; every write is a plain brightness value on the one LED node.
static int g_bl_fd = -1;
static int g_bl_level = -1;

static void backlight_set(int level) {
    if (g_bl_fd < 0 || level == g_bl_level) {
        return;
    }
    char buf[8];
    int n = snprintf(buf, sizeof buf, "%d\n", level);
    if (write(g_bl_fd, buf, (size_t)n) == n) {
        g_bl_level = level;
    } else {
        printf("charging: backlight write failed: %s\n", strerror(errno));
    }
}

#define BACKLIGHT_ON_LEVEL 255 // the same as the player's default

// The panel: /dev/fb0 mapped once, XRGB8888, drawn into the next page and shown with a pan (a write alone is not
// guaranteed to reach the panel). Without a usable mode nothing is drawn and the backlight is never lit.
typedef struct {
    int fd;
    uint8_t *map;
    size_t map_len;
    struct fb_var_screeninfo var;
    uint32_t line_bytes;
    unsigned pages, cur;
    bool ok;
} panel_t;

static void panel_open(panel_t *p) {
    memset(p, 0, sizeof *p);
    p->fd = open(Y2_FB_DEVICE, O_RDWR | O_CLOEXEC);
    if (p->fd < 0) {
        printf("charging: cannot open %s: %s\n", Y2_FB_DEVICE, strerror(errno));
        return;
    }
    struct fb_fix_screeninfo fix;
    if (ioctl(p->fd, FBIOGET_FSCREENINFO, &fix) != 0 || ioctl(p->fd, FBIOGET_VSCREENINFO, &p->var) != 0) {
        printf("charging: FBIOGET_*SCREENINFO failed: %s\n", strerror(errno));
        return;
    }
    const struct fb_var_screeninfo *v = &p->var;
    if (!y2_fb_mode_is_xrgb8888(v->bits_per_pixel, (y2_fb_field_t){v->red.offset, v->red.length},
                                (y2_fb_field_t){v->green.offset, v->green.length}, (y2_fb_field_t){v->blue.offset, v->blue.length},
                                (y2_fb_field_t){v->transp.offset, v->transp.length})) {
        printf("charging: unexpected framebuffer mode %ux%u %ubpp, no picture\n", v->xres, v->yres, v->bits_per_pixel);
        return;
    }
    p->map_len = fix.smem_len;
    p->line_bytes = fix.line_length;
    void *m = mmap(NULL, p->map_len, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    if (m == MAP_FAILED) {
        printf("charging: framebuffer mmap failed: %s\n", strerror(errno));
        return;
    }
    p->map = m;
    p->pages = y2_fb_page_count(p->map_len, v->yres_virtual, p->line_bytes, v->yres);
    p->cur = v->yres > 0 ? v->yoffset / v->yres : 0; // a previous run can leave another page showing: never draw onto it
    if (p->cur >= p->pages) {
        p->cur = 0;
    }
    p->ok = true;
}

static int ioctl_retry(int fd, unsigned long req, void *arg) {
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r != 0 && errno == EINTR);
    return r;
}

// The same two steps as the player's display HAL (hal_display.c pan_to): FBIOPAN_DISPLAY, then FBIOPUT_VSCREENINFO with
// FB_ACTIVATE_NOW, which is what makes mtkfb latch the new page.
static bool panel_pan(panel_t *p, unsigned page) {
    struct fb_var_screeninfo var = p->var;
    var.xoffset = 0;
    var.yoffset = page * p->var.yres;
    if (ioctl_retry(p->fd, FBIOPAN_DISPLAY, &var) != 0) {
        return false;
    }
    var.activate = FB_ACTIVATE_NOW;
    return ioctl_retry(p->fd, FBIOPUT_VSCREENINFO, &var) == 0;
}

// Draw one scene into the next page and show it (black frame when @p scene is NULL).
static void panel_show(panel_t *p, const y2_cview_scene_t *scene) {
    if (!p->ok) {
        return;
    }
    unsigned page = y2_fb_next_page(p->cur, p->pages);
    uint8_t *base = p->map + (size_t)page * p->line_bytes * p->var.yres;
    if (scene != NULL) {
        y2_cview_render((uint32_t *)(void *)base, p->var.xres, p->var.yres, p->line_bytes / 4u, scene);
    } else {
        memset(base, 0, (size_t)p->line_bytes * p->var.yres);
    }
    if (panel_pan(p, page)) {
        p->cur = page;
    }
}

static void panel_close(panel_t *p) {
    if (p->map != NULL) {
        munmap(p->map, p->map_len);
    }
    if (p->fd >= 0) {
        close(p->fd);
    }
}

// Read one node. Wake on any wake event; only the side-key node (@p keys) feeds the Power-hold logic.
static void drain(int fd, y2_charge_t *c, bool keys, bool *woke, bool *pressed) {
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
            if (y2_cview_is_key_press(&ev[i])) {
                *pressed = true;
            } else if (y2_cview_is_wake_event(&ev[i])) {
                *woke = true;
            }
            if (keys) {
                y2_charge_feed(c, &ev[i], now);
            }
        }
        if ((size_t)r < sizeof buf) {
            return;
        }
    }
}

static int open_node(const char *name) {
    char path[64];
    if (!y2_input_find_node(Y2_INPUT_CLASS, Y2_DEV_INPUT, name, path, sizeof path)) {
        printf("charging: no evdev node named %s\n", name);
        return -1;
    }
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("charging: cannot open %s: %s\n", path, strerror(errno));
    }
    return fd;
}

// Gauge for the picture; the scene keeps its last values if a read fails (unknown until the first good one).
static void read_gauge(y2_cview_scene_t *scene, bool *logged) {
    hal_battery_status_t st;
    y2_power_status_t ps = Y2_PS_UNKNOWN;
    if (!y2_power_read(Y2_POWER_SUPPLY, &st, &ps)) {
        if (!*logged) {
            printf("charging: no battery reading, showing --%%\n");
            *logged = true;
        }
        return;
    }
    scene->pct = st.soc_pct;
    scene->full = ps == Y2_PS_FULL || ps == Y2_PS_NOT_CHARGING;
}

int y2_charging_main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term; // no SA_RESTART: a signal ends the sleep at once
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    // Only the display backlight and the framebuffer are written; the side keys and the wheel are read (to wake the screen).
    g_bl_fd = open(Y2_BACKLIGHT, O_WRONLY | O_CLOEXEC);
    if (g_bl_fd < 0) {
        printf("charging: no backlight control at %s: %s\n", Y2_BACKLIGHT, strerror(errno));
    }
    panel_t panel;
    panel_open(&panel);
    panel_show(&panel, NULL); // black, so nothing stale (the boot splash, a previous run) shows before the first picture
    backlight_set(0);

    int fds[2] = {open_node("mtk-kpd"), open_node("mtk-tpd")}; // side keys (incl. Power), wheel
    if (fds[0] < 0) {
        printf("charging: Power key unavailable\n");
    }
    printf("charging: charging-only mode (battery animation for %u s after entry or a key), Power held 2 s reboots, unplug for 3 s powers off\n",
           Y2_CVIEW_AWAKE_MS / 1000u);
    fflush(stdout);

    y2_charge_t c;
    y2_charge_init(&c);
    y2_cview_t view;
    y2_cview_init(&view, now_ms());
    y2_cview_scene_t scene = {.pct = Y2_CVIEW_PCT_UNKNOWN};
    bool gauge_logged = false;
    int rc = 0;
    while (!g_term) {
        bool woke = false, pressed = false;
        for (int i = 0; i < 2; i++) {
            if (fds[i] >= 0) {
                drain(fds[i], &c, i == 0, &woke, &pressed);
            }
        }
        uint32_t now = now_ms();
        if (pressed) {
            y2_cview_wake(&view, now);
        } else if (woke) {
            y2_cview_wake_motion(&view, now);
        }
        y2_charge_online_t online = y2_charge_read_online(Y2_POWER_SUPPLY);
        y2_charge_action_t act = y2_charge_step(&c, now, online);
        // The screen: asleep costs nothing (no gauge read, no drawing); awake redraws only when something changed.
        if (y2_cview_is_awake(&view, now)) {
            read_gauge(&scene, &gauge_logged);
            scene.full = scene.full && online != Y2_CHARGE_OFFLINE; // "charged" only with the charger present
            scene.hold_ms = y2_charge_hold_ms(&c, now);
        }
        unsigned todo = y2_cview_tick(&view, now, &scene);
        if (todo & Y2_CVIEW_DRAW) {
            panel_show(&panel, &scene);
        }
        if (todo & Y2_CVIEW_BACKLIGHT_ON) {
            backlight_set(panel.ok ? BACKLIGHT_ON_LEVEL : 0); // never light a panel we cannot draw on
        }
        if ((todo & Y2_CVIEW_BACKLIGHT_OFF) || !y2_cview_is_awake(&view, now)) {
            backlight_set(0); // also retried every pass while asleep, in case an earlier write failed (no-op once it is 0)
        }
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
    for (int i = 0; i < 2; i++) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    panel_close(&panel);
    if (g_bl_fd >= 0) {
        close(g_bl_fd);
    }
    printf("charging: exiting (%d)\n", rc);
    fflush(stdout);
    return rc;
}
