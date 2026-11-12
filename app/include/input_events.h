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
    INPUT_POWER_DOWN,  /**< the Power key went down (Hold gesture, screen sleep; docs/adr/0020-hold.md) */
    INPUT_POWER_UP,    /**< ... and up again. The input task never drops this one (a lost release would count as a 10 s hold) */
    INPUT_HOLD_SWITCH, /**< the hardware Hold switch moved: steps = 1 locked, 0 unlocked */
    INPUT_HOLD_TOGGLE, /**< toggle Hold directly (console key 'l': boards without a Power key) */
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
 * m or Backspace = Menu, l = toggle Hold (INPUT_HOLD_TOGGLE). A button key gives a press event then a release event; a scroll key gives
 * one INPUT_SCROLL of +/-4 positions (one list step, as the wheel will). Unknown keys give no events.
 * Returns the number of events written (at most @p max).
 */
size_t input_events_from_key(int key, input_event_t *out, size_t max);

/**
 * The input task's hand-over to the UI queue (pure logic, so a test can drive it with a small fake queue). Wheel and key events are best effort: when
 * the queue is full they are dropped and counted (UI-6: a burst of presses faster than the UI drains them used to vanish silently). The Power key and
 * the Hold switch are LEVELS reported as edges that must not be lost (a lost Power release would count as a 10 s hold and power the player off), so
 * they are retried on every pass until the queue takes them.
 */
typedef bool (*input_send_fn)(void *ctx, const input_event_t *ev);
typedef struct {
    bool power_sent;               /**< the Power level the UI was last told */
    hal_hold_switch_t switch_sent; /**< the Hold switch position the UI was last told (NONE until a switch reports) */
    uint32_t dropped;              /**< events dropped because the queue was full (diagnostics: the monitor prints it) */
} input_pump_t;

void input_pump_init(input_pump_t *p);
/** Send @p n events; a full queue drops the rest of them (counted). */
void input_pump_send(input_pump_t *p, input_send_fn send, void *ctx, const input_event_t *ev, size_t n);
/** Report changes of the Power key and the Hold switch; a change the queue refuses is retried by the next call. */
void input_pump_levels(input_pump_t *p, input_send_fn send, void *ctx, bool power_down, hal_hold_switch_t sw);

/** Events the input task had to drop since boot because the UI queue was full (0 on a healthy run). */
uint32_t rt_input_events_dropped(void);

/** Number of valid wheel frames the input task has seen (0 until a real wheel talks); for the boot check. */
uint32_t rt_wheel_frames_seen(void);

/** Queue (input task -> UI) of input_event_t; NULL until the runtime has created it. */
os_queue_t *rt_input_queue(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_INPUT_EVENTS_H
