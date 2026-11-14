// SPDX-License-Identifier: MPL-2.0
#include "touch_gesture.h"

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

void touch_gesture_init(touch_gesture_t *g, uint16_t panel_w, uint16_t panel_h) {
    *g = (touch_gesture_t){0};
    g->slop_px = (int32_t)panel_h / TOUCH_SLOP_DIV;
    if (g->slop_px < TOUCH_SLOP_MIN_PX) {
        g->slop_px = TOUCH_SLOP_MIN_PX;
    }
    g->swipe_px = (int32_t)panel_w / TOUCH_SWIPE_DIV;
    g->px_per_position = ((int32_t)panel_h / TOUCH_ROW_DIV) / TOUCH_POSITIONS_PER_STEP;
    if (g->px_per_position < 1) {
        g->px_per_position = 1;
    }
}

void touch_gesture_cancel(touch_gesture_t *g) { g->state = TOUCH_WAIT_LIFT; }

// A button press and its release, as the wheel would report them.
static size_t press(uint8_t button, input_event_t *out, size_t max) {
    if (max < 2) {
        return 0;
    }
    out[0] = (input_event_t){.type = INPUT_BUTTON_DOWN, .button = button};
    out[1] = (input_event_t){.type = INPUT_BUTTON_UP, .button = button};
    return 2;
}

size_t touch_gesture_update(touch_gesture_t *g, const hal_touch_point_t *pt, uint32_t now_ms, input_event_t *out, size_t max) {
    if (g->state == TOUCH_WAIT_LIFT) {
        if (!pt->down) {
            g->state = TOUCH_IDLE;
        }
        return 0;
    }
    if (!pt->down) {
        // A still finger lifted: a tap, or (if the polls skipped past the long-press time, e.g. a busy bus) the long press it was.
        const bool pending = g->state == TOUCH_PENDING;
        const bool was_tap = (uint32_t)(now_ms - g->t0) < TOUCH_LONG_PRESS_MS;
        g->state = TOUCH_IDLE;
        return pending ? press(was_tap ? HAL_BTN_SELECT : HAL_BTN_PLAY, out, max) : 0;
    }
    if (g->state == TOUCH_IDLE) {
        g->state = TOUCH_PENDING;
        g->x0 = pt->x;
        g->y0 = pt->y;
        g->t0 = now_ms;
        g->sent = 0;
        return 0;
    }
    const int32_t dx = (int32_t)pt->x - g->x0;
    const int32_t dy = (int32_t)pt->y - g->y0;
    if (g->state == TOUCH_PENDING) {
        if (iabs(dx) > g->slop_px || iabs(dy) > g->slop_px) {
            g->state = iabs(dy) >= iabs(dx) ? TOUCH_SCROLL : TOUCH_SWIPE; // the first axis to leave the slop decides, for the rest of the touch
        } else if ((uint32_t)(now_ms - g->t0) >= TOUCH_LONG_PRESS_MS) {
            g->state = TOUCH_SPENT;
            return press(HAL_BTN_PLAY, out, max);
        } else {
            return 0;
        }
    }
    if (g->state == TOUCH_SCROLL) {
        // Total positions from the touch-down point (finger up = positive), so jitter cannot drift and going back undoes what was sent.
        int32_t want = (g->y0 - (int32_t)pt->y) / g->px_per_position;
        int32_t delta = want - g->sent;
        if (delta > 127) {
            delta = 127; // one event carries an int8: the rest follows on the next sample
        } else if (delta < -127) {
            delta = -127;
        }
        if (delta == 0 || max < 1) {
            return 0;
        }
        g->sent += delta;
        out[0] = (input_event_t){.type = INPUT_SCROLL, .steps = (int8_t)delta};
        return 1;
    }
    if (g->state == TOUCH_SWIPE && dx >= g->swipe_px) {
        g->state = TOUCH_SPENT;
        return press(HAL_BTN_MENU, out, max);
    }
    return 0;
}
