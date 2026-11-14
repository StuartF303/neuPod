// SPDX-License-Identifier: MPL-2.0
/**
 * @file touch_gesture.h
 * @brief Single-point touch to the click wheel's input events (portable logic, host-unit-tested; docs/ui-input.md "Touch").
 *
 * The UI keeps its click-wheel vocabulary (scroll, Select, Menu, Play); a touch panel is the stand-in for the wheel on boards that have
 * none. Distances scale with the panel so the same hand movement works on a 320x240 and a 600x450 panel (4:3 both):
 *
 *  - Vertical drag = INPUT_SCROLL. One list row of finger travel (panel height / 8, the row pitch of every profile) is one list step
 *    (TOUCH_POSITIONS_PER_STEP wheel positions), counted from where the finger came down. Natural, phone-like direction: the finger
 *    moves UP, the content moves up, the highlight goes DOWN the list (positive steps, as clockwise on the wheel); dragging down goes back up.
 *    The UI's scroll acceleration (ui_accel_feed) applies as it does to the wheel, so a quick drag travels further.
 *  - Tap (lifted within TOUCH_LONG_PRESS_MS, moved less than the slop) = Select press and release.
 *  - Horizontal swipe right (a drag that starts horizontal and travels a fifth of the panel width) = Menu press and release (back), once
 *    per touch. A swipe left does nothing.
 *  - Long press (still for TOUCH_LONG_PRESS_MS) = Play press and release, once per touch, fired when the time is up (not at lift).
 *
 * Hold needs no code here: the events enter the same queue as the wheel's, and the UI swallows them while locked or asleep exactly as it
 * does the wheel's (ui_frame.c).
 */
#ifndef NEUPOD_TOUCH_GESTURE_H
#define NEUPOD_TOUCH_GESTURE_H

#include <stddef.h>
#include <stdint.h>

#include "hal_touch.h"
#include "input_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOUCH_LONG_PRESS_MS 600u  /**< held still this long = Play */
#define TOUCH_POSITIONS_PER_STEP 4 /**< wheel positions per list step (as the wheel and the console keys: input_events.h) */
#define TOUCH_SLOP_DIV 20          /**< movement that is no longer a tap: panel height / 20 (12 px at 240, 22 px at 450), at least TOUCH_SLOP_MIN_PX */
#define TOUCH_SLOP_MIN_PX 8
#define TOUCH_SWIPE_DIV 5          /**< horizontal travel for Menu: panel width / 5 (64 px at 320, 120 px at 600) */
#define TOUCH_ROW_DIV 8            /**< list rows per panel height (7 rows + the status bar): a row of travel = panel height / 8 */

typedef enum {
    TOUCH_IDLE = 0, /**< no finger */
    TOUCH_PENDING,  /**< finger down, not decided yet (tap or long press if it lifts or waits) */
    TOUCH_SCROLL,   /**< locked to a vertical drag */
    TOUCH_SWIPE,    /**< locked to a horizontal drag, Menu not sent yet */
    TOUCH_SPENT,    /**< this touch already produced its event (long press, swipe); ignore it until the finger lifts */
} touch_gesture_state_t;

typedef struct {
    touch_gesture_state_t state;
    int32_t slop_px, swipe_px, px_per_position; /**< from the panel size */
    int32_t x0, y0;                             /**< where the finger came down */
    uint32_t t0;                                /**< ... and when (ms) */
    int32_t sent;                               /**< wheel positions already sent for this drag (positive = down the list) */
} touch_gesture_t;

/** Set up for a @p panel_w x @p panel_h panel (the display's current size). */
void touch_gesture_init(touch_gesture_t *g, uint16_t panel_w, uint16_t panel_h);

/**
 * Feed one sample (@p pt->down false = no finger) taken at @p now_ms; writes up to @p max events (2 are enough for everything but a scroll, which
 * is one event) and returns how many. Call it at the polling cadence even while the finger is still: the long press is timed by the call.
 */
size_t touch_gesture_update(touch_gesture_t *g, const hal_touch_point_t *pt, uint32_t now_ms, input_event_t *out, size_t max);

/** Forget the touch in progress without sending anything (the controller stopped answering: a tap must not come from a read error). */
void touch_gesture_cancel(touch_gesture_t *g);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_TOUCH_GESTURE_H
