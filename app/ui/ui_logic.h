// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui_logic.h
 * @brief UI maths and bookkeeping with no LVGL dependency (host-unit-tested): list selection and
 *        scrolling, wheel-scroll acceleration, and the screen stack.
 */
#ifndef NEUPOD_UI_LOGIC_H
#define NEUPOD_UI_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
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
/** The three-bar monochrome meter of the Bluetooth lists from the 0..4 bars of ui_signal_bars(): 0, 1, 2, 2, 3. */
int ui_signal_bars3(int bars);

/**
 * What a found Bluetooth device is, from its Class of Device, for the Devices list: "" for an audio device (the only kind that can be a
 * headset), otherwise a word for the row's "(phone)": "phone", "computer", "wearable", "other" (peripherals, an unknown class).
 */
const char *ui_bt_class_word(uint32_t cod);

/* ---- Backlight auto-dim: idle clock and effective level ------------------------------------------- */

#define UI_DIM_PCT 10u          /**< the idle backlight level, absolute; a set level below it is left alone */
#define UI_IDLE_FUTURE_MS 120000u /**< how far ahead of the tick clock an input stamp may be (the tour spaces the 260 presses of "260*s" 300 ms apart: 78 s) */
#define UI_AUTODIM_STEPS 6u     /**< entries of the "Dim after" choice */
#define UI_AUTODIM_DEFAULT_IDX 2u /**< 30 s */

/** Seconds of the "Dim after" choice @p idx (0..UI_AUTODIM_STEPS-1: 10 s, 15 s, 30 s, 1 min, 2 min, 5 min); 0 if out of range. */
uint32_t ui_autodim_seconds(uint8_t idx);
/** Label of the "Dim after" choice @p idx ("30 s", "1 min"); "-" if out of range. */
const char *ui_autodim_name(uint8_t idx);

#define UI_SCREENOFF_STEPS 7u       /**< entries of the "Screen off after" choice */
#define UI_SCREENOFF_DEFAULT_IDX 5u /**< 3 min (after the dim) */

/** Seconds of the "Screen off after" choice @p idx (0 = Never, 15 s, 30 s, 1 min, 2 min, 3 min, 5 min); 0 (never) if out of range. */
uint32_t ui_screenoff_seconds(uint8_t idx);
/** Label of the "Screen off after" choice @p idx ("3 min", "Never"); "-" if out of range. */
const char *ui_screenoff_name(uint8_t idx);

/** Settings > Display > Theme (design review DS-1). Stored as a u8; Light is the default. */
typedef enum { UI_THEME_LIGHT = 0, UI_THEME_DARK = 1, UI_THEME_AUTO = 2, UI_THEME_MODES } ui_theme_mode_t;
/** Decode a stored value; false (and @p out untouched) when it is not a mode. */
bool ui_theme_mode_from_stored(uint8_t v, ui_theme_mode_t *out);
const char *ui_theme_mode_name(ui_theme_mode_t m); /**< "Light", "Dark", "Auto" */
ui_theme_mode_t ui_theme_mode_next(ui_theme_mode_t m); /**< Light -> Dark -> Auto -> Light */
/** Is the dark theme in force: Dark always, Auto while the backlight is dimmed (or off), Light never. */
bool ui_theme_is_dark(ui_theme_mode_t m, bool dimmed);

typedef struct {
    uint32_t last_ms; /**< time of the last input (or of the start) */
    bool dimmed;
    bool off;        /**< the backlight is fully off (second stage after the dim, or Power pressed briefly): the waking input is not delivered */
    bool forced_off; /**< off because the user asked (short Power press), not because of the idle time: stays until input */
} ui_idle_t;

/** Start the idle clock at @p now_ms (not at 0: a long boot must not count as idle). */
void ui_idle_init(ui_idle_t *t, uint32_t now_ms);
/** Input happened at @p now_ms (may be ahead of the tick clock in the host tour). True if that ended a dim or an off. */
bool ui_idle_activity(ui_idle_t *t, uint32_t now_ms);
/** Re-evaluate: dimmed exactly when @p enabled and at least @p timeout_ms have passed since the last input (wrap-safe;
 *  a last input up to UI_IDLE_FUTURE_MS ahead of @p now_ms is not idle). While @p inhibit is set it is never dimmed and the
 *  clock restarts at @p now_ms. True if the dimmed state changed. */
bool ui_idle_tick(ui_idle_t *t, uint32_t now_ms, bool enabled, bool inhibit, uint32_t timeout_ms);
/**
 * The second stage: off exactly when the screen is forced off (ui_idle_force_off) or @p off_after_ms (0 = never) has passed beyond the dim
 * time (@p dim_enabled ? @p dim_timeout_ms : 0) since the last input. Never while @p inhibit is set. Same future-stamp tolerance as
 * ui_idle_tick(). True if the off state changed. Call after ui_idle_tick() (which restarts the clock while inhibited).
 */
bool ui_idle_off_tick(ui_idle_t *t, uint32_t now_ms, bool dim_enabled, bool inhibit, uint32_t dim_timeout_ms, uint32_t off_after_ms);
/** Turn the screen off now (a short Power press); it stays off until input. True if that changed anything. */
bool ui_idle_force_off(ui_idle_t *t);
/** Backlight percent to drive: 0 while @p off, else ui_backlight_effective(). */
uint8_t ui_backlight_level(uint8_t set_pct, bool dimmed, bool off);
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

#define UI_SEEK_IDLE_MS 10000u  /**< seek mode is cancelled (nothing applied) this long after its last input, so a forgotten seek never keeps the wheel */
#define UI_SEEK_COMMIT_MS 350u  /**< a fast-forward / rewind scrub (hold Left / Right) is applied this long after it stops; seek mode applies on Select only */
#define UI_SELHOLD_MS 500u      /**< Select held this long enters seek mode (a shorter press opens page 2) */
#define UI_HOLD_MS 600u         /**< a Left / Right press held this long is a fast-forward / rewind, not a skip */
#define UI_FF_SPEED 10u         /**< hold: track seconds covered per second held */
#define UI_HOLD_MAX_MS 30000u   /**< a hold ends itself this long after the key went down (a release that never arrives must not leave it running) */

typedef enum { UI_WHEEL_VOLUME = 0, UI_WHEEL_SEEK } ui_wheel_mode_t;
/*
 * Now Playing keys (owner decision, 12 Nov 2026): Select short press = page 2; HOLD Select (UI_SELHOLD_MS) = seek mode; in seek mode the wheel moves the
 * target, Select applies it ("go") and Menu drops it ("cancel"). Hold Left / Right still scans the track at UI_FF_SPEED times and applies on release.
 */

/** Event flags returned by the ui_npc_* calls (a bit set). */
enum {
    UI_NPC_NONE = 0,
    UI_NPC_COMMIT = 1u << 0,      /**< apply ui_npc_t.target now (player_seek) */
    UI_NPC_TO_VOLUME = 1u << 1,   /**< the wheel went back to volume (idle, or the track cannot seek) */
    UI_NPC_SKIP = 1u << 2,        /**< a Left / Right press that was not a hold: skip (key_up) */
    UI_NPC_UNAVAILABLE = 1u << 3, /**< a hold on a track that cannot seek: say "Seek not available" */
    UI_NPC_PAGE2 = 1u << 4,       /**< a short Select press was released: open page 2 */
    UI_NPC_SEEK_ENTER = 1u << 5,  /**< the Select hold reached UI_SELHOLD_MS and seek mode began: tick the haptic */
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
    uint32_t id;        /**< the track the pending target belongs to (ui_npc_track_t.id) */
    uint32_t hold_id;   /**< the track the hold started on */
    bool sel_down;      /**< Select is down (its press began a possible hold) */
    bool sel_used;      /**< ... and was already used: the hold fired, or it applied / cancelled a seek: the release does nothing */
    uint32_t sel_ms;    /**< when Select went down */
} ui_npc_t;

/** What the playing track offers (from player_info_t). */
typedef struct {
    bool seekable;
    uint64_t pos_frames, total_frames;
    uint32_t rate;
    uint32_t id; /**< changes when another track plays (player_info_t.track_id): a scrub or hold never carries over to it */
} ui_npc_track_t;

void ui_npc_init(ui_npc_t *c);
/** Select went down. In seek mode that is "go": the target is applied at once (UI_NPC_COMMIT when one is pending) and seek mode ends. Otherwise it starts
 *  a possible hold (ui_npc_tick() enters seek mode after UI_SELHOLD_MS). */
uint32_t ui_npc_select_down(ui_npc_t *c, uint32_t now_ms);
/** Select went up: UI_NPC_PAGE2 for a short press that was not used for anything else. */
uint32_t ui_npc_select_up(ui_npc_t *c);
/** Menu in seek mode: drop the target and leave seek mode (true when it was in seek mode: the key is consumed; false: Menu goes back as usual). */
bool ui_npc_menu(ui_npc_t *c);
/** Wheel steps (already accelerated): in seek mode they move the target (true), else the caller changes the volume (false). */
bool ui_npc_scroll(ui_npc_t *c, const ui_npc_track_t *t, int32_t steps, uint32_t now_ms);
/** Left / Right went down: a candidate skip or hold. */
void ui_npc_key_down(ui_npc_t *c, int dir, const ui_npc_track_t *t, uint32_t now_ms);
/** Forget a Left / Right that is down (another key, the wheel, or the screen going away): its release does nothing, a hold in progress is dropped. */
void ui_npc_cancel_hold(ui_npc_t *c);
/** A target is pending and it belongs to the track that plays now (apply it only then). */
bool ui_npc_commit_valid(const ui_npc_t *c, const ui_npc_track_t *t);
/** It went up: UI_NPC_SKIP when it was a short press, UI_NPC_COMMIT when a hold moved the target. */
uint32_t ui_npc_key_up(ui_npc_t *c, int dir, uint32_t now_ms);
/** Periodic: the Select hold, the idle cancel of seek mode, the commit of a Left / Right scrub once it stopped, the fast-forward while held. */
uint32_t ui_npc_tick(ui_npc_t *c, const ui_npc_track_t *t, uint32_t now_ms);
/** The target was handed to the player. */
void ui_npc_committed(ui_npc_t *c);
/** The position to draw: the target while one is pending, else the playing position. */
uint64_t ui_npc_position(const ui_npc_t *c, const ui_npc_track_t *t);
/** Track seconds one (accelerated) wheel step moves in seek mode: about 1/60 of the track, 1..30 s. */
uint32_t ui_seek_step_s(uint64_t total_s);

/* ---- Display text ---- */

/**
 * True when the UI fonts (app/ui/fonts, tools/gen_ui_fonts.sh) have a glyph for code point @p cp: ASCII, Latin-1 without the soft hyphen,
 * a few typographic marks (en/em dash, curly quotes, bullet, ellipsis, euro) and the LV_SYMBOL_* the UI draws. Keep it in step with the
 * ranges in the font script; a host test checks it against the compiled fonts.
 */
bool ui_text_has_glyph(uint32_t cp);

/**
 * Copy @p raw (UTF-8) to @p out (capacity @p n) for drawing in a label: every character the fonts have is kept as it is; the rest becomes
 * what draws instead of an empty box: nothing (zero-width marks, combining accents), a space (other control characters, odd spaces), an
 * ASCII look-alike (other dashes and quotes), the base letter(s) (Latin Extended-A: Ł -> L, Œ -> OE; fullwidth forms), or '?' (CJK, emoji,
 * other scripts; one '?' per character) so the length stays honest. '\n' is kept. Malformed UTF-8 becomes '?'; a sequence cut off at the
 * end is dropped. The result is never longer than the input, so it may be built in a buffer of strlen(raw) + 1, and the copy is cut only
 * at a character boundary. The stored string is never changed. Always NUL-terminates (n > 0).
 */
void ui_text_display(char *out, size_t n, const char *raw);

/** True when ui_text_display() would return @p raw unchanged (the fast path: no copy needed). */
bool ui_text_is_clean(const char *raw);

/* ---- Screen stack (opaque entries) ---------------------------------------------------------------- */

#define UI_STACK_MAX 10

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
