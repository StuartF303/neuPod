// SPDX-License-Identifier: MPL-2.0
/**
 * @file input_events.h
 * @brief Turn successive click-wheel states into discrete UI events (button edges, scroll).
 */
#ifndef NEUPOD_INPUT_EVENTS_H
#define NEUPOD_INPUT_EVENTS_H

#include <stddef.h>
#include <stdint.h>

#include "hal_input.h"
#include "os.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_BUTTON_DOWN,
    INPUT_BUTTON_UP,
    INPUT_SCROLL,
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    uint8_t button; /**< HAL_BTN_* bit, for button events */
    int8_t steps;   /**< Signed wheel positions moved (clockwise positive), for INPUT_SCROLL */
} input_event_t;

/**
 * Compare @p cur with the previous valid state @p prev and write up to @p max events. Scroll is
 * reported only while the finger stays on the wheel across both frames (a lifted finger that lands
 * elsewhere does not scroll), using the shortest way round the 96-position ring. Returns the
 * number of events written.
 */
size_t input_events_diff(const hal_wheel_state *prev, const hal_wheel_state *cur,
                         input_event_t *out, size_t max);

/**
 * Testbed keyboard (UART console on the board) to events. Keys: w/k scroll up (counter-clockwise),
 * s/j scroll down, Enter or e = Select, a = Left, d = Right, p or Space = Play/Pause,
 * m or Backspace = Menu. A button key gives a press event then a release event; a scroll key gives
 * one INPUT_SCROLL of +/-4 positions (one list step, as the wheel will). Unknown keys give no events.
 * Returns the number of events written (at most @p max).
 */
size_t input_events_from_key(int key, input_event_t *out, size_t max);

/** Number of valid wheel frames the input task has seen (0 until a real wheel talks); for the boot check. */
uint32_t rt_wheel_frames_seen(void);

/** Queue (input task -> UI) of input_event_t; NULL until the runtime has created it. */
os_queue_t *rt_input_queue(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_INPUT_EVENTS_H
