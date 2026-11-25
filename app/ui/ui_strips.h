// SPDX-License-Identifier: MPL-2.0
// Frame-private side of the toast / HUD / lock-pill strips (ui_strips.c). The public calls (ui_toast_show, ui_hud_show) are in ui_internal.h.
#ifndef NEUPOD_UI_STRIPS_H
#define NEUPOD_UI_STRIPS_H

#include "lvgl.h"

/** Build the strips on the frame's screen (before the status bar, so the toast slides out from under it). */
void ui_strips_init(lv_obj_t *scr);
/** Build the Power-hold line (after the status bar: it draws over the bar's lower edge). */
void ui_strips_init_line(lv_obj_t *scr);
/** Time-outs of the toast and the HUD (every UI pass). */
void ui_strips_tick(uint32_t now_ms);
/** Jump a toast / HUD slide to its end state. */
void ui_strips_settle(void);

/** The lock pill at the bottom: a dark pill with one line of Small text, no dimming of what is behind it. */
void ui_pill_show(const char *text);
void ui_pill_hide(void);
/** The Power-hold progress line under the status bar: @p pct 0..100 of the width, accent or (@p red) the power-off countdown; pct < 0 hides it. */
void ui_hold_line_set(int pct, bool red);

#endif
