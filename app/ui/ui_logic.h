// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui_logic.h
 * @brief UI maths and bookkeeping with no LVGL dependency (host-unit-tested): list selection and
 *        scrolling, wheel-scroll acceleration, and the screen stack.
 */
#ifndef NEUPOD_UI_LOGIC_H
#define NEUPOD_UI_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- List navigation: selection clamps at both ends (no wrap), the window follows the selection --- */

typedef struct {
    int32_t count;   /**< items */
    int32_t sel;     /**< selected item, 0 when the list is empty */
    int32_t visible; /**< rows that fit on screen */
    int32_t top;     /**< first row of the window */
} ui_nav_t;

void ui_nav_init(ui_nav_t *n, int32_t count, int32_t visible);
/** Move the selection by @p delta (clamped). True if it changed. The window scrolls just enough to keep it visible. */
bool ui_nav_move(ui_nav_t *n, int32_t delta);
/** Select @p sel (clamped), centring it in the window where possible. */
void ui_nav_set(ui_nav_t *n, int32_t sel);
/** Change the item count (after the model changed), keeping the selection in range. */
void ui_nav_set_count(ui_nav_t *n, int32_t count);

/* ---- Wheel scroll acceleration -------------------------------------------------------------------- */

typedef struct {
    int32_t residue;    /**< counts not yet worth a step (signed) */
    uint32_t energy;    /**< decaying count of recent movement */
    uint32_t last_ms;
} ui_accel_t;

void ui_accel_init(ui_accel_t *a);
/**
 * Feed @p counts (signed wheel positions) at time @p now_ms; returns whole list steps to move.
 * UI_COUNTS_PER_STEP counts make a step; fast movement (many counts within UI_ACCEL_WINDOW_MS)
 * multiplies it by 2, 4 or 8 (runtime_config.h).
 */
int32_t ui_accel_feed(ui_accel_t *a, int32_t counts, uint32_t now_ms);

/* ---- Signal meter ----------------------------------------------------------------------------------- */

/** RSSI (dBm) thresholds for 4, 3 and 2 bars; weaker than b2 is 1 bar. */
typedef struct {
    int32_t b4, b3, b2;
} ui_signal_thr_t;

extern const ui_signal_thr_t k_ui_signal_wifi; /**< runtime_config.h UI_SIG_WIFI_* */
extern const ui_signal_thr_t k_ui_signal_bt;   /**< runtime_config.h UI_SIG_BT_* */

/**
 * Bars (1..4) for @p rssi_dbm. @p prev is the bars last shown (0 = none: no hysteresis); with a previous value, the bars
 * rise only once the signal is hyst_db above a threshold and fall only once it is hyst_db below it (use UI_SIG_HYST_DB).
 */
int ui_signal_bars(int32_t rssi_dbm, const ui_signal_thr_t *thr, int prev, int32_t hyst_db);
/** "Excellent", "Good", "Fair" or "Weak" for 4..1 bars (the Settings summary pane). */
const char *ui_signal_word(int bars);
/** Colour class of a bar count: 3 = green (3 or 4 bars), 2 = yellow (2 bars), 1 = red (1 bar). */
int ui_signal_class(int bars);

/* ---- Screen stack (opaque entries) ---------------------------------------------------------------- */

#define UI_STACK_MAX 8

typedef struct {
    void *items[UI_STACK_MAX];
    int depth;
} ui_stack_t;

void ui_stack_init(ui_stack_t *s);
bool ui_stack_push(ui_stack_t *s, void *item); /**< false if full */
void *ui_stack_pop(ui_stack_t *s);             /**< NULL if empty */
void *ui_stack_top(const ui_stack_t *s);
void *ui_stack_below_top(const ui_stack_t *s); /**< the entry that becomes visible after a pop */

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_UI_LOGIC_H
