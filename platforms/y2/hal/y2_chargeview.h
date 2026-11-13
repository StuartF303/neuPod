// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_chargeview.h
 * @brief Charging-only mode screen, portable half: an animated battery drawn straight into an XRGB8888 buffer (no LVGL, no
 *        RTOS: charging mode starts neither), and the screen-on timer. Unit-tested on the PC, which also writes PPM
 *        snapshots at 320x240 and 480x360. The framebuffer, backlight and battery reads are in platforms/y2/charging.c.
 *
 * Look: the dark theme tokens of the player (ui_theme.c) as literals, 5 segments that fill one by one up to the current
 * level and pause there, then start again from empty; the percentage below and a caption. Gauge full / not charging
 * (with the charger present) shows a still full battery captioned CHARGED. Own artwork only.
 * Time: the screen is on Y2_CVIEW_AWAKE_MS after entering the mode and after every key press; then the backlight goes
 * off and nothing is drawn until the next key.
 */
#ifndef NEUPOD_Y2_CHARGEVIEW_H
#define NEUPOD_Y2_CHARGEVIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "y2_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_CVIEW_AWAKE_MS 10000u
#define Y2_CVIEW_FRAME_MS 600u /**< one animation step */
#define Y2_CVIEW_CELLS 5u
#define Y2_CVIEW_PCT_UNKNOWN 255u /**< no gauge reading: empty battery, "--%" */
#define Y2_CVIEW_LOW_PCT 15u /**< at or below this the fill is red */
/** Power hold shown as a bar under the battery; the reboot itself is y2_charge's 2 s. */
#define Y2_CVIEW_HOLD_FULL_MS 2000u

/** True for events that wake the screen: a key press, a wheel or touch movement (not releases, sync or misc). */
bool y2_cview_is_wake_event(const y2_event_t *ev);

/** Segments lit at 100 % of the animation for @p pct: ceil(pct / 20), 0..5 (0 for Y2_CVIEW_PCT_UNKNOWN). */
unsigned y2_cview_target_cells(unsigned pct);

/** Segments lit in animation step @p frame: 0, 1, ... target, then the full level held one more step; all when @p full. */
unsigned y2_cview_cells_shown(unsigned frame, unsigned target, bool full);

/** Animation steps in one cycle for @p target (empty, each segment, the hold). */
unsigned y2_cview_frame_count(unsigned target);

typedef struct {
    uint8_t pct;          /**< 0..100, or Y2_CVIEW_PCT_UNKNOWN */
    bool full;            /**< gauge full / not charging while the charger is present: still battery, CHARGED */
    unsigned frame;       /**< animation step (ignored when full) */
    uint32_t hold_ms;     /**< Power held so far (0 = not held): shows "hold to restart" and a bar */
} y2_cview_scene_t;

/** Draw the whole scene into @p px (@p w x @p h pixels, rows @p stride_px apart). Any size from 160x120 up. */
void y2_cview_render(uint32_t *px, unsigned w, unsigned h, unsigned stride_px, const y2_cview_scene_t *s);

/** What the loop has to do after y2_cview_tick(). */
#define Y2_CVIEW_BACKLIGHT_ON 1u
#define Y2_CVIEW_BACKLIGHT_OFF 2u
#define Y2_CVIEW_DRAW 4u

typedef struct {
    bool awake;
    uint32_t wake_ms;
    bool have_drawn;
    y2_cview_scene_t drawn;
} y2_cview_t;

/** Start awake at @p now_ms (the first tick asks for the backlight on and a draw). */
void y2_cview_init(y2_cview_t *v, uint32_t now_ms);

/** A key: stay (or become) awake for another Y2_CVIEW_AWAKE_MS. */
void y2_cview_wake(y2_cview_t *v, uint32_t now_ms);

/**
 * One loop pass. @p scene->frame is filled in here from the time since waking (the caller sets pct, full, hold_ms).
 * Returns a mask of Y2_CVIEW_*: BACKLIGHT_ON when the screen has just become awake (draw first, light second), DRAW when
 * the scene changed since the last draw (animation step, level, state, hold bar), BACKLIGHT_OFF once the awake time is up.
 * While asleep it returns 0 and the caller should not read the battery or draw.
 */
unsigned y2_cview_tick(y2_cview_t *v, uint32_t now_ms, y2_cview_scene_t *scene);

/** Is the screen on (so the caller should read the battery for this pass)? Handles the timeout without a scene. */
bool y2_cview_is_awake(const y2_cview_t *v, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_CHARGEVIEW_H
