// SPDX-License-Identifier: MPL-2.0
/**
 * @file wheel_input.h
 * @brief Wheel input widgets, MODEL layer (docs/ui-input.md): pure C, no LVGL, host-unit-tested.
 *
 * Each widget is a state struct plus wi_<widget>_event(): event in, new state out, and a wi_result_t saying whether the
 * widget is still running, was confirmed (WI_OK) or cancelled. The LVGL view (app/ui/input/ui_input.c) draws the state and
 * feeds the events. Key semantics are the same everywhere:
 *   scroll  = change the thing under the cursor (character, value, choice); steps arrive already accelerated
 *   Select  = choose / type / toggle / next field
 *   Menu    = back: backspace when there is something to delete, else cancel
 *   Play    = confirm / done
 *   Left/Right = move the cursor, or the field
 */
#ifndef NEUPOD_WHEEL_INPUT_H
#define NEUPOD_WHEEL_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WI_EV_SCROLL, WI_EV_SELECT, WI_EV_MENU, WI_EV_PLAY, WI_EV_LEFT, WI_EV_RIGHT } wi_ev_t;
typedef enum { WI_RUNNING = 0, WI_OK, WI_CANCELLED } wi_result_t;

/* ---- Text entry (keyboard) and its search mode ---- */

#define WI_TEXT_MAX 64u /* bytes of text, excluding the NUL */
typedef enum { WI_CS_ANY, WI_CS_DIGITS, WI_CS_HEX } wi_charset_t;
typedef enum { WI_PAGE_LOWER, WI_PAGE_UPPER, WI_PAGE_DIGITS, WI_PAGE_SYMBOLS, WI_PAGE_COUNT } wi_page_t;
typedef enum { WI_KEY_CHAR, WI_KEY_PAGE, WI_KEY_SPACE, WI_KEY_SHOW, WI_KEY_DONE } wi_key_kind_t;

typedef struct {
    char text[WI_TEXT_MAX + 1];
    uint32_t len;       /* bytes */
    uint32_t cursor;    /* byte offset, always on a UTF-8 boundary */
    uint32_t max_chars; /* code points, <= WI_TEXT_MAX */
    uint32_t max_bytes; /* byte limit (<= WI_TEXT_MAX); for fields that are limited in bytes, e.g. an SSID */
    wi_charset_t charset;
    bool masked;        /* password: shown as dots unless show */
    bool show;
    bool search;        /* incremental: `changed` is raised after every edit */
    bool changed;       /* set by an edit; the view/caller clears it after reading */
    wi_page_t page;
    uint32_t sel;       /* index into the current key strip */
} wi_text_t;

void wi_text_init(wi_text_t *t, const char *initial, uint32_t max_chars, wi_charset_t cs, bool masked, bool search);
/** Limit the text to @p max_bytes bytes (0 = WI_TEXT_MAX); truncates the current text at a character boundary if it is longer. */
void wi_text_set_byte_limit(wi_text_t *t, uint32_t max_bytes);
wi_result_t wi_text_event(wi_text_t *t, wi_ev_t ev, int32_t steps);
/** Type a code point directly (host keyboard shortcut / UART). False if rejected (charset, length). */
bool wi_text_insert(wi_text_t *t, uint32_t cp);
uint32_t wi_text_chars(const wi_text_t *t);
/** The strip of the current page: number of keys and the key at @p i (character text for WI_KEY_CHAR, label otherwise). */
uint32_t wi_text_strip_len(const wi_text_t *t);
wi_key_kind_t wi_text_strip_key(const wi_text_t *t, uint32_t i, char out[8]);

/* ---- Number / spinner (fixed point: value is in units of 10^-decimals) ---- */

typedef struct {
    int32_t value, initial, min, max, step;
    uint8_t decimals;
    const char *unit;
    bool changed;
} wi_number_t;
void wi_number_init(wi_number_t *n, int32_t value, int32_t min, int32_t max, int32_t step, uint8_t decimals, const char *unit);
wi_result_t wi_number_event(wi_number_t *n, wi_ev_t ev, int32_t steps);
/** "-12.5 dB" style text. */
void wi_number_format(const wi_number_t *n, char *out, size_t cap);

/* ---- Slider / level: a number with a live preview (changed is raised on every move); Menu restores the initial value ---- */
typedef wi_number_t wi_slider_t;
void wi_slider_init(wi_slider_t *s, int32_t value, int32_t min, int32_t max, int32_t step, const char *unit);
wi_result_t wi_slider_event(wi_slider_t *s, wi_ev_t ev, int32_t steps);

/* ---- Choice picker, single and multiple ---- */

#define WI_PICK_MAX 32u
typedef struct {
    uint32_t count, sel, mask; /* mask: ticked items (multi) */
    bool multi, wrap;
} wi_picker_t;
void wi_picker_init(wi_picker_t *p, uint32_t count, uint32_t sel, bool multi, bool wrap);
wi_result_t wi_picker_event(wi_picker_t *p, wi_ev_t ev, int32_t steps);

/* ---- Date and time ---- */

typedef struct {
    int32_t y, mo, d, h, mi;
    bool has_date, has_time;
    uint8_t field; /* 0 day 1 month 2 year 3 hour 4 minute (only those present are visited) */
} wi_datetime_t;
bool wi_is_leap(int32_t y);
int32_t wi_days_in_month(int32_t y, int32_t mo);
void wi_datetime_init(wi_datetime_t *t, bool has_date, bool has_time, int32_t y, int32_t mo, int32_t d, int32_t h, int32_t mi);
wi_result_t wi_datetime_event(wi_datetime_t *t, wi_ev_t ev, int32_t steps);

/* ---- Letter jump overlay for long sorted lists ---- */

typedef struct {
    bool active;
    char letter;       /* 'A'..'Z', '#' for anything else */
    uint32_t last_ms;
    uint32_t energy;   /* decaying count of recent steps */
} wi_letterjump_t;
#define WI_LJ_FAST_STEPS 6u   /* steps inside the window that count as "fast" */
#define WI_LJ_WINDOW_MS 400u
#define WI_LJ_HIDE_MS 250u
void wi_letterjump_init(wi_letterjump_t *j);
/** Feed a scroll of @p steps at @p now_ms; @p initial is the first byte of the (sorted) item now under the cursor. Returns active. */
bool wi_letterjump_scroll(wi_letterjump_t *j, int32_t steps, uint32_t now_ms, char initial);
/** Call from the UI tick: hides the overlay after WI_LJ_HIDE_MS without scrolling. */
void wi_letterjump_tick(wi_letterjump_t *j, uint32_t now_ms);
/** Contract: the list must be sorted by the NORMALISED initial: '#' (anything not A-Z, case-insensitive) first, then A..Z. */
/** Index of the first item whose initial is >= @p letter in a list sorted by initial (binary search over @p initial_of). */
int32_t wi_letter_first_index(int32_t count, char (*initial_of)(void *ctx, int32_t idx), void *ctx, char letter);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_WHEEL_INPUT_H
