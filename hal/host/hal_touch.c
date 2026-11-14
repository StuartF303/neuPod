// SPDX-License-Identifier: MPL-2.0
/*
 * Host touch simulator: with NEUPOD_TOUCH=1 the mouse in the simulator window is the touch panel (left button = finger down, the
 * pointer = the contact, in panel pixels), so the touch gesture path (app/input/touch_gesture.c) can be driven on the desktop. Without it
 * the host has no touch panel, like the Pico 2 W and the Y2 (hal_touch_none.c). The mouse wheel stays the click wheel either way.
 * The tours (ui.c script tokens touch:down:X:Y, touch:move:X:Y, touch:up) inject contacts through hal_host_touch_inject() instead
 * (they set NEUPOD_TOUCH=1 too, so the panel is present).
 */
#include <stdlib.h>

#include "hal_display.h"
#include "hal_touch.h"
#include "host_internal.h"

// Latest contact packed into one word (x in bits 0..15, y in 16..30, down in bit 31): the UI thread (SDL pump, tour script) writes, the input task reads.
static uint32_t g_contact;

static bool enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("NEUPOD_TOUCH");
        cached = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    return cached != 0;
}

void hal_host_touch_inject(int x, int y, bool down) {
    const int w = hal_display_width(), h = hal_display_height();
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    __atomic_store_n(&g_contact, (uint32_t)x | ((uint32_t)y << 16) | (down ? 0x80000000u : 0u), __ATOMIC_RELAXED);
}

void host_touch_mouse(int x, int y, bool down) {
    if (enabled()) {
        hal_host_touch_inject(x, y, down);
    }
}

bool hal_touch_present(void) { return enabled(); }

hal_status_t hal_touch_init(void) { return enabled() ? HAL_OK : HAL_ENOTSUP; }

bool hal_touch_read(hal_touch_point_t *out) {
    if (out == NULL) {
        return false;
    }
    const uint32_t c = __atomic_load_n(&g_contact, __ATOMIC_RELAXED);
    out->x = (uint16_t)(c & 0xFFFFu);
    out->y = (uint16_t)((c >> 16) & 0x7FFFu);
    out->down = (c & 0x80000000u) != 0;
    return true;
}
