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
 * Bars (1..4) for @p rssi_dbm. A reading of 0 dBm or more is "unknown" (a failed radio read, HAL_WIFI_RSSI_UNKNOWN, or
 * HAL_BT_RSSI_UNKNOWN): it returns @p prev (held), i.e. 0 = no bars when there is none. @p prev is the bars last shown (0 = none: no hysteresis); with a previous value, the bars
 * rise only once the signal is hyst_db above a threshold and fall only once it is hyst_db below it (use UI_SIG_HYST_DB).
 */
int ui_signal_bars(int32_t rssi_dbm, const ui_signal_thr_t *thr, int prev, int32_t hyst_db);
/** "Excellent", "Good", "Fair", "Weak" for 4..1 bars, "-" for 0 (no reading) (the Settings summary pane). */
const char *ui_signal_word(int bars);
/** Colour class of a bar count: 3 = green (3 or 4 bars), 2 = yellow (2 bars), 1 = red (1 bar). */
int ui_signal_class(int bars);

/* ---- Backlight auto-dim: idle clock and effective level ------------------------------------------- */

#define UI_DIM_PCT 10u          /**< the idle backlight level, absolute; a set level below it is left alone */
#define UI_IDLE_FUTURE_MS 60000u /**< how far ahead of the tick clock an input stamp may be (the tour spaces events by 300 ms) */
#define UI_AUTODIM_STEPS 6u     /**< entries of the "Dim after" choice */
#define UI_AUTODIM_DEFAULT_IDX 2u /**< 30 s */

/** Seconds of the "Dim after" choice @p idx (0..UI_AUTODIM_STEPS-1: 10 s, 15 s, 30 s, 1 min, 2 min, 5 min); 0 if out of range. */
uint32_t ui_autodim_seconds(uint8_t idx);
/** Label of the "Dim after" choice @p idx ("30 s", "1 min"); "-" if out of range. */
const char *ui_autodim_name(uint8_t idx);

typedef struct {
    uint32_t last_ms; /**< time of the last input (or of the start) */
    bool dimmed;
} ui_idle_t;

/** Start the idle clock at @p now_ms (not at 0: a long boot must not count as idle). */
void ui_idle_init(ui_idle_t *t, uint32_t now_ms);
/** Input happened at @p now_ms (may be ahead of the tick clock in the host tour). True if that ended a dim. */
bool ui_idle_activity(ui_idle_t *t, uint32_t now_ms);
/** Re-evaluate: dimmed exactly when @p enabled and at least @p timeout_ms have passed since the last input (wrap-safe;
 *  a last input up to UI_IDLE_FUTURE_MS ahead of @p now_ms is not idle). While @p inhibit is set it is never dimmed and the
 *  clock restarts at @p now_ms. True if the dimmed state changed. */
bool ui_idle_tick(ui_idle_t *t, uint32_t now_ms, bool enabled, bool inhibit, uint32_t timeout_ms);
/** Level (percent) to show: the set level, capped at UI_DIM_PCT while dimmed. Never brighter than @p set_pct. */
uint8_t ui_backlight_effective(uint8_t set_pct, bool dimmed);

/* ---- Global transport keys ------------------------------------------------------------------------ */

typedef enum { UI_KEY_NONE = 0, UI_KEY_BACK, UI_KEY_PAUSE, UI_KEY_NEXT, UI_KEY_PREV } ui_key_action_t;
/**
 * What a button does on ANY screen when the top screen did not consume it (ui_frame_input): Menu = back, Play = pause / resume, and
 * Right / Left = next / previous track while a queue is playing (@p queue_active). Select does nothing globally (screens own it).
 */
ui_key_action_t ui_global_key(uint8_t btn, bool queue_active);

/* ---- Now Playing controls: wheel mode (volume / seek), scrubbing, hold-to-fast-forward ---------------------- */

#define UI_SEEK_IDLE_MS 3000u   /**< the wheel drops back from seek to volume this long after the last seek-mode input (the iPod's 3 s) */
#define UI_SEEK_COMMIT_MS 350u  /**< a scrub is applied (one restart of the track) this long after the wheel stops */
#define UI_HOLD_MS 600u         /**< a Left / Right press held this long is a fast-forward / rewind, not a skip */
#define UI_FF_SPEED 10u         /**< hold: track seconds covered per second held */

typedef enum { UI_WHEEL_VOLUME = 0, UI_WHEEL_SEEK } ui_wheel_mode_t;

/** Event flags returned by the ui_npc_* calls (a bit set). */
enum {
    UI_NPC_NONE = 0,
    UI_NPC_COMMIT = 1u << 0,      /**< apply ui_npc_t.target now (player_seek) */
    UI_NPC_TO_VOLUME = 1u << 1,   /**< the wheel went back to volume (idle, or the track cannot seek) */
    UI_NPC_SKIP = 1u << 2,        /**< a Left / Right press that was not a hold: skip (key_up) */
    UI_NPC_UNAVAILABLE = 1u << 3, /**< a hold on a track that cannot seek: say "Seek not available" */
};

typedef struct {
    ui_wheel_mode_t mode;
    uint32_t mode_ms;   /**< last seek-mode input */
    bool pending;       /**< target moved and not applied yet: show it instead of the playing position */
    uint64_t target;    /**< frames */
    uint32_t move_ms;   /**< when target last moved */
    int8_t hold_dir;    /**< Left (-1) / Right (+1) is down, 0 none */
    bool holding;       /**< ... for UI_HOLD_MS already */
    uint32_t hold_ms;   /**< when it went down */
    uint32_t step_ms;   /**< last fast-forward step */
} ui_npc_t;

/** What the playing track offers (from player_info_t). */
typedef struct {
    bool seekable;
    uint64_t pos_frames, total_frames;
    uint32_t rate;
} ui_npc_track_t;

void ui_npc_init(ui_npc_t *c);
/** Select: volume -> seek -> volume. A track that cannot seek stays on volume (false: show "Seek not available"). */
bool ui_npc_select(ui_npc_t *c, const ui_npc_track_t *t, uint32_t now_ms);
/** Wheel steps (already accelerated): in seek mode they move the target (true), else the caller changes the volume (false). */
bool ui_npc_scroll(ui_npc_t *c, const ui_npc_track_t *t, int32_t steps, uint32_t now_ms);
/** Left / Right went down: a candidate skip or hold. */
void ui_npc_key_down(ui_npc_t *c, int dir, uint32_t now_ms);
/** It went up: UI_NPC_SKIP when it was a short press, UI_NPC_COMMIT when a hold moved the target. */
uint32_t ui_npc_key_up(ui_npc_t *c, int dir, uint32_t now_ms);
/** Periodic: idle fall-back to volume, the scrub commit once the wheel rested, the fast-forward while held. */
uint32_t ui_npc_tick(ui_npc_t *c, const ui_npc_track_t *t, uint32_t now_ms);
/** The target was handed to the player. */
void ui_npc_committed(ui_npc_t *c);
/** The position to draw: the target while one is pending, else the playing position. */
uint64_t ui_npc_position(const ui_npc_t *c, const ui_npc_track_t *t);
/** Track seconds one (accelerated) wheel step moves in seek mode: about 1/60 of the track, 1..30 s. */
uint32_t ui_seek_step_s(uint64_t total_s);

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
