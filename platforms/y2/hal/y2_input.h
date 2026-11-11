// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_input.h
 * @brief Innioasis Y2 input, portable half: evdev frame decoding, node discovery by name, and the
 *        mapping onto the app's click-wheel model. No device access here, so it is unit-tested on the PC.
 *
 * The Y2's wheel controller (mtk-tpd) decodes rotation itself and reports one KEY_DOWN (clockwise) or
 * KEY_UP (anticlockwise) press+release per detent; the ring buttons come from the same node. Side keys
 * come from mtk-kpd. The app expects a click-wheel state (hal_wheel_state), so a detent moves a virtual
 * finger by Y2_POSITIONS_PER_DETENT positions: the same 4 positions a console 'w'/'s' key sends, which is
 * one list step, so acceleration (done in the UI from the scroll events) behaves as on the real wheel.
 */
#ifndef NEUPOD_Y2_INPUT_H
#define NEUPOD_Y2_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Linux input-event codes the Y2 uses (linux/input-event-codes.h, copied so the PC build needs no kernel headers). */
#define Y2_EV_KEY 1u
#define Y2_KEY_ENTER 28u
#define Y2_KEY_UP 103u
#define Y2_KEY_LEFT 105u
#define Y2_KEY_RIGHT 106u
#define Y2_KEY_DOWN 108u
#define Y2_KEY_VOLUMEDOWN 114u
#define Y2_KEY_VOLUMEUP 115u
#define Y2_KEY_POWER 116u
#define Y2_KEY_PLAYPAUSE 164u
#define Y2_KEY_BACK 158u

#define Y2_POSITIONS_PER_DETENT 4
/** A wheel tick this soon after a ring-button press is a finger brushing the wheel, not a turn. */
#define Y2_TICK_AFTER_PRESS_IGNORE_MS 150u

/** struct input_event on a 32-bit kernel: timeval (2 x 32 bit), type, code, value. Reads must be multiples of this. */
#define Y2_EVENT_SIZE 16u

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} y2_event_t;

/**
 * Decode up to @p max events from @p len bytes of raw evdev data (little endian, 16 bytes each, the
 * timestamp is dropped). A trailing partial record is ignored; @p consumed (may be NULL) reports the
 * whole records used so the caller can keep the remainder.
 */
size_t y2_evdev_decode(const uint8_t *buf, size_t len, y2_event_t *out, size_t max, size_t *consumed);

/** Mapping state: what the virtual wheel looks like to the app. */
typedef struct {
    uint8_t buttons;        /**< HAL_BTN_* bits currently held */
    uint8_t position;       /**< virtual finger position 0..HAL_WHEEL_POSITIONS-1 */
    bool dirty;             /**< state changed since y2_input_take() */
    bool have_press;        /**< a ring button was pressed (last_press_ms is valid) */
    uint32_t last_press_ms;
    int32_t volume_steps;   /**< pending side-key volume steps (+ louder), taken with y2_input_take_volume() */
    uint32_t ticks_dropped; /**< wheel ticks ignored by the brush filter (diagnostics) */
    uint32_t power_presses; /**< KEY_POWER presses seen (diagnostics; not mapped to an app action) */
} y2_input_t;

void y2_input_init(y2_input_t *s);

/** Feed one decoded event at monotonic time @p now_ms. Non-key events and unknown codes are ignored. */
void y2_input_feed(y2_input_t *s, const y2_event_t *ev, uint32_t now_ms);

/** Copy the current wheel state to @p out (always valid, finger always "on" the wheel) and clear dirty. True if it changed. */
bool y2_input_take(y2_input_t *s, hal_wheel_state *out);

/** Return and clear the pending volume steps. */
int32_t y2_input_take_volume(y2_input_t *s);

/**
 * Find an input node by device name: looks for <class_dir>/event<N>/device/name equal to @p name and
 * writes "<dev_dir>/event<N>" to @p out. Names are matched whole (mtk-tpd does not match mtk-tpd-kpd).
 * False if none matches.
 */
bool y2_input_find_node(const char *class_dir, const char *dev_dir, const char *name, char *out, size_t cap);

/**
 * Filter for the remote key channels (control FIFO, piped stdin): `echo s > /run/neupod.keys` delivers 's' and a
 * newline, and the newline must not count as Enter (Select; use 'e'). On a terminal Enter is a real key. Returns the
 * key to pass to input_events_from_key(), or -1 to drop it.
 */
int y2_console_key(int c, bool from_tty);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_INPUT_H
