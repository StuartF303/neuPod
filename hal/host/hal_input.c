// SPDX-License-Identifier: MPL-2.0
/*
 * Host click-wheel simulator. Keyboard/mouse events (see host_window.c) update a simulated
 * wheel state; hal_input_poll() encodes that into a real 32-bit wheel frame and runs it through
 * clickwheel_decode(), so the host exercises the same decoder as the hardware path.
 */
#include "clickwheel_frame.h"
#include "hal_input.h"
#include "hal_system.h"
#include "host_internal.h"

#define SCROLL_STEPS_PER_NOTCH 4 // wheel positions per mouse-wheel notch (= one UI list step)
#define TOUCH_HOLD_MS 250u       // finger stays "on the wheel" this long after a scroll

static hal_wheel_state g_sim; // buttons/position/touched as the simulated wheel would report
static uint32_t g_touch_until;
static bool g_dirty;

void host_input_button(uint8_t btn, bool down) {
    uint8_t before = g_sim.buttons;
    g_sim.buttons = down ? (uint8_t)(g_sim.buttons | btn) : (uint8_t)(g_sim.buttons & ~btn);
    if (g_sim.buttons != before) {
        g_dirty = true;
    }
}

void host_input_scroll(int steps) {
    int pos = ((int)g_sim.position + steps * SCROLL_STEPS_PER_NOTCH) % HAL_WHEEL_POSITIONS;
    if (pos < 0) {
        pos += HAL_WHEEL_POSITIONS;
    }
    g_sim.position = (uint8_t)pos;
    g_sim.touched = true;
    g_touch_until = hal_millis() + TOUCH_HOLD_MS;
    g_dirty = true;
}

hal_status_t hal_input_init(void) {
    g_sim = (hal_wheel_state){0};
    g_dirty = true;
    return HAL_OK;
}

bool hal_input_is_simulated(void) { return true; }

hal_status_t hal_input_reinit(void) { return HAL_OK; } // nothing to re-arm on the host

bool hal_input_poll(hal_wheel_state *out) {
    host_pump_events();
    if (g_sim.touched && (int32_t)(hal_millis() - g_touch_until) >= 0) {
        g_sim.touched = false;
        g_dirty = true;
    }
    hal_wheel_state decoded;
    bool ok = clickwheel_decode(clickwheel_encode(&g_sim), &decoded);
    bool fresh = ok && g_dirty;
    g_dirty = false;
    if (out != NULL) {
        *out = decoded;
    }
    return fresh;
}
