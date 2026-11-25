// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui_input.h
 * @brief Modal wheel input: ask the user for text, a number, a level, a choice or a date/time (docs/ui-input.md).
 *
 * ui_input_open() pushes a modal screen (one at a time). The models live in wheel_input.h; this is their LVGL view and the
 * request/result API. LVGL objects exist only while the modal is open. UI task only.
 */
#ifndef NEUPOD_UI_INPUT_H
#define NEUPOD_UI_INPUT_H

#include "wheel_input.h"

typedef enum { WI_K_TEXT, WI_K_NUMBER, WI_K_SLIDER, WI_K_PICKER, WI_K_DATETIME } wi_kind_t;

typedef struct {
    char text[WI_TEXT_MAX + 1]; /**< text */
    int32_t number;             /**< number, slider (in units of 10^-decimals) */
    uint32_t pick, mask;        /**< picker: chosen item, ticked items (multi) */
    wi_datetime_t dt;           /**< date/time */
} wi_value_t;

typedef void (*wi_done_fn)(void *ctx, bool ok, const wi_value_t *v); /**< called once, before the modal closes */
typedef void (*wi_change_fn)(void *ctx, const wi_value_t *v);       /**< live: every edit of a search text, every slider move (and the revert on cancel) */

typedef struct {
    wi_kind_t kind;
    const char *title;
    wi_done_fn done;
    wi_change_fn change;
    void *ctx;
    /* text */
    const char *initial;
    uint32_t max_chars;
    uint32_t max_bytes; /**< 0 = WI_TEXT_MAX; Wi-Fi names and passwords are limited in BYTES (32 / 63), not characters */
    wi_charset_t charset;
    bool masked, search;
    /* number, slider */
    int32_t value, min, max, step;
    uint8_t decimals;
    const char *unit;
    /* picker */
    const char *const *items;
    uint32_t count, sel;
    bool multi;
    /* date/time */
    bool has_date, has_time;
    int32_t y, mo, d, h, mi;
} wi_request_t;

/** False if a modal is already open or memory is short. The request (not its strings) is copied; strings must outlive the call only. */
bool ui_input_open(const wi_request_t *r);
bool ui_input_active(void);

/* ---- Letter-jump overlay for long sorted lists ---- */
typedef struct ui_letter_overlay ui_letter_overlay_t;
/** Create (hidden) on @p parent (the screen's root): a tile at the top right with the letter, an index rail down the right edge (ui_rail.c). */
ui_letter_overlay_t *ui_letter_overlay_create(void *lv_parent);
/** Show or hide according to @p j (call after wi_letterjump_scroll / _tick). */
void ui_letter_overlay_update(ui_letter_overlay_t *o, const wi_letterjump_t *j);
void ui_letter_overlay_destroy(ui_letter_overlay_t *o);
/** Tours only: keep the overlay up (the scripted steps are slower than the hide delay), so a snapshot can show it. */
void ui_letter_overlay_tour_hold(bool on);

#endif
