// SPDX-License-Identifier: MPL-2.0
// Private to app/ui: shared colours and the screen interfaces.
#ifndef NEUPOD_UI_INTERNAL_H
#define NEUPOD_UI_INTERNAL_H

#include "battery_store.h"
#include "capabilities.h"
#include "input_events.h"
#include "lvgl.h"

#define UI_W 320
#define UI_H 240

// Palette: deep blue-black ground, one cyan accent, red only for "not working".
#define UI_COL_BG 0x0A1020
#define UI_COL_BG2 0x16263F
#define UI_COL_BAR 0x1B2A3F
#define UI_COL_ACCENT 0x2EC4F1
#define UI_COL_TEXT 0xE8EEF5
#define UI_COL_DIM 0x6C7B8F
#define UI_COL_ERR 0xFF4D4F

/** Build the splash screen and load it. */
void ui_splash_create(void);
/** Reflect the capabilities (check results) in the icon row. Cheap; call every frame. */
void ui_splash_update(const capabilities_t *caps);

/**
 * Splash hold (a check failed): show the draining bar and cycle the failure reasons, one line at a
 * time. @p elapsed_ms of @p total_ms have passed.
 */
void ui_splash_hold_update(const capabilities_t *caps, uint32_t elapsed_ms, uint32_t total_ms);

/** Build the home screen (not loaded). */
lv_obj_t *ui_home_create(const capabilities_t *caps);
/** Battery percent in the status bar; @p have false shows "--". */
void ui_home_set_battery(bool have, const hal_battery_status_t *bat);
/** Show the latest input event (proves the input path). */
void ui_home_set_input(const input_event_t *ev);

#endif // NEUPOD_UI_INTERNAL_H
