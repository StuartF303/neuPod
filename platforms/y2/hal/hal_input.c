// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 input: the click wheel and ring buttons (mtk-tpd) and the side keys (mtk-kpd) are Linux evdev nodes found
 * by device name. The decoding and the mapping onto the app's wheel model are in y2_input.c (unit-tested); this
 * file only opens the nodes and drains them without blocking. hal_input_poll() is called every 2 ms by the
 * runtime's input task, the only caller.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hal_input.h"
#include "hal_system.h"
#include "y2_hal.h"
#include "y2_input.h"

typedef struct {
    const char *name; // sysfs device name
    int fd;
} node_t;

static node_t g_nodes[] = {{"mtk-tpd", -1}, {"mtk-kpd", -1}};
static y2_input_t g_in;
static void (*g_volume_fn)(int32_t);

void y2_input_set_volume_handler(void (*fn)(int32_t steps)) { g_volume_fn = fn; }

// The Power key is only reported here (level, for the input task to turn into events); what it means (a short press puts the screen
// to sleep, 2 s toggles Hold, 10 s powers off) is decided in the UI (app/ui/ui_hold.c, docs/adr/0020-hold.md).
bool hal_input_power_key(void) { return y2_input_power_down(&g_in); }
bool hal_input_has_power_key(void) { return true; }

// The Y2 has no Hold slide switch (the iPod 6G donor board has one).
hal_hold_switch_t hal_input_hold_switch(void) { return HAL_HOLD_SWITCH_NONE; }

hal_status_t hal_input_init(void) {
    y2_input_init(&g_in);
    unsigned opened = 0;
    for (size_t i = 0; i < sizeof g_nodes / sizeof g_nodes[0]; i++) {
        if (g_nodes[i].fd >= 0) {
            close(g_nodes[i].fd);
            g_nodes[i].fd = -1;
        }
        char path[64];
        if (!y2_input_find_node(Y2_INPUT_CLASS, Y2_DEV_INPUT, g_nodes[i].name, path, sizeof path)) {
            printf("input: no evdev node named %s\n", g_nodes[i].name);
            continue;
        }
        g_nodes[i].fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (g_nodes[i].fd < 0) {
            printf("input: cannot open %s (%s): %s\n", path, g_nodes[i].name, strerror(errno));
            continue;
        }
        printf("input: %s on %s\n", g_nodes[i].name, path);
        opened++;
    }
    return opened > 0 ? HAL_OK : HAL_ENODEV;
}

// The wheel is a simulation as far as the app is concerned (a virtual finger): always a live input source.
bool hal_input_is_simulated(void) { return true; }

hal_status_t hal_input_reinit(void) { return HAL_OK; }

static void drain(node_t *n) {
    uint8_t buf[Y2_EVENT_SIZE * 32];
    for (;;) {
        ssize_t r = read(n->fd, buf, sizeof buf); // always a whole number of 16-byte events
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                printf("input: %s read failed (%s), node closed\n", n->name, strerror(errno));
                close(n->fd);
                n->fd = -1;
                if (n == &g_nodes[1]) { // mtk-kpd carries KEY_POWER: a held press would never see its release
                    y2_input_power_lost(&g_in);
                }
            }
            return;
        }
        if (r == 0) {
            return;
        }
        y2_event_t ev[32];
        size_t used = 0;
        size_t cnt = y2_evdev_decode(buf, (size_t)r, ev, 32, &used);
        uint32_t now = hal_millis();
        for (size_t i = 0; i < cnt; i++) {
            y2_input_feed(&g_in, &ev[i], now);
        }
        if ((size_t)r < sizeof buf) {
            return;
        }
    }
}

// The input task calls this every 2 ms; on a battery-powered device that would be 1000 syscalls a second for nothing, so the
// nodes are looked at every Y2_POLL_PERIOD_MS (events wait in the kernel queue meanwhile; nothing is lost).
#define Y2_POLL_PERIOD_MS 8u

bool hal_input_poll(hal_wheel_state *out) {
    static uint32_t last_ms;
    static bool first = true;
    uint32_t now = hal_millis();
    if (first || (uint32_t)(now - last_ms) >= Y2_POLL_PERIOD_MS) {
        first = false;
        last_ms = now;
        for (size_t i = 0; i < sizeof g_nodes / sizeof g_nodes[0]; i++) {
            if (g_nodes[i].fd >= 0) {
                drain(&g_nodes[i]);
            }
        }
    }
    int32_t vol = y2_input_take_volume(&g_in);
    if (vol != 0 && g_volume_fn != NULL) {
        g_volume_fn(vol);
    }
    return y2_input_take(&g_in, out);
}
