// SPDX-License-Identifier: MPL-2.0
// Private to app/ui: shared colours, the screen framework and the list widget.
#ifndef NEUPOD_UI_INTERNAL_H
#define NEUPOD_UI_INTERNAL_H

#include "battery_store.h"
#include "capabilities.h"
#include "haptic_logic.h"
#include "input_events.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_hold.h"
#include "ui_icons.h"
#include "ui_logic.h"
#include "ui_metrics.h"
#include "ui_theme.h"

/* Geometry comes from the active metrics profile (ui_metrics.h), chosen at UI init from the panel size. */
#define UIM (ui_metrics())
#define UI_W (UIM->w)
#define UI_H (UIM->h)

// Boot splash palette: deep blue-black ground, one cyan accent, red only for "not working". Literal colours: the splash is not themed.
#define UI_COL_BG 0x0A1020
#define UI_COL_BG2 0x16263F
#define UI_COL_BAR 0x1B2A3F
#define UI_COL_ACCENT 0x2EC4F1
#define UI_COL_TEXT 0xE8EEF5
#define UI_COL_DIM 0x6C7B8F
#define UI_COL_ERR 0xFF4D4F

// Everything else draws with the design-system tokens in ui_theme.h (UI_C_INK, UI_C_ACCENT ...): they follow Settings > Display > Theme.
// The names the screens used before the tokens existed, kept so a colour reads by its role:
#define UI_L_BG UI_C_BG
#define UI_L_PANE UI_C_SURFACE
#define UI_L_LINE UI_C_LINE
#define UI_L_TEXT UI_C_INK
#define UI_L_DIM UI_C_INK2
#define UI_L_FAINT UI_C_INK3
#define UI_L_SEL UI_C_ACCENT
#define UI_L_OK UI_C_GREEN
#define UI_L_WARN UI_C_AMBER
#define UI_L_ERR UI_C_RED

#define UI_STATUS_H (UIM->status_h)
#define UI_CONTENT_H (UIM->content_h)
#define UI_ROW_H (UIM->row_h)
#define UI_ROWS_VISIBLE (UIM->rows)

#ifdef NEUPOD_UI_HIRES
/** Hosted builds only (ui_audit.c): count the visible widgets of the active screen that are cut by the panel or their container. */
int ui_layout_audit(const char *screen);
/** Marks the list's pool rows for the audit: rows outside the visible window are clipped away on purpose. */
#define UI_OBJ_FLAG_POOL_ROW LV_OBJ_FLAG_USER_1
#endif

/* ---- Boot splash (ui_splash.c) -------------------------------------------------------------------- */

void ui_splash_create(void);
void ui_splash_update(const capabilities_t *caps);
void ui_splash_hold_update(const capabilities_t *caps, uint32_t elapsed_ms, uint32_t total_ms);

/* ---- Screen framework (ui_frame.c) ---------------------------------------------------------------- */

typedef struct ui_screen ui_screen_t;
struct ui_screen {
    const char *title;
    lv_obj_t *root;                                   /* child of the content area, UI_W x UI_CONTENT_H */
    void (*on_scroll)(ui_screen_t *s, int32_t steps); /* list steps (already accelerated) */
    bool (*on_button)(ui_screen_t *s, uint8_t btn);   /* HAL_BTN_*, on press; true = handled (else Menu pops) */
    void (*on_button_up)(ui_screen_t *s, uint8_t btn); /* optional: HAL_BTN_*, on release (hold detection); no return value */
    void (*on_tick)(ui_screen_t *s);                  /* every UI pass while on top (rate-limit inside) */
    void (*destroy)(ui_screen_t *s);                  /* release resources (the object itself is freed after) */
    void (*suspend)(ui_screen_t *s);                  /* optional: another screen was pushed over this one (free what can be rebuilt) */
    void (*resume)(ui_screen_t *s);                   /* optional: it is on top again */
    bool suspended;
    bool shows_volume; /* the screen draws the volume itself (Now Playing, Test tone): the volume HUD stays away while it is on top */
    char left[40]; /* text of the status bar's left slot while this screen is on top (ui_screen_set_left) */
    void *ud;
};

/** Persistent state the screens and Settings share (RAM only for now). */
typedef struct {
    click_mode_t click_mode; /* what answers every list step: piezo, haptic, both or nothing */
    uint8_t backlight_pct; /* 5..100 */
    bool autodim;          /* dim the backlight after the idle time */
    uint8_t dim_idx;       /* idle time, an index for ui_autodim_seconds() */
    uint8_t off_idx;       /* time after the dim until the backlight is fully off, an index for ui_screenoff_seconds() */
    bool autolock;         /* Hold turns on by itself when the screen goes off (docs/adr/0020-hold.md) */
    ui_theme_mode_t theme; /* Settings > Display > Theme: Light (default), Dark, Auto (dark once the backlight dims) */
} ui_state_t;
extern ui_state_t g_ui_state;
/** Show the backlight level on the panel: the set level, or the dimmed one while idle. The only place that computes it. */
void ui_apply_backlight(void);
/** Apply g_ui_state.theme now (the Settings row changed it, or the settings store was loaded). Auto follows the dim state at the next tick. */
void ui_apply_theme(void);
/** True while Hold is on (the screens get no input; ui_hold.h). */
bool ui_frame_locked(void);
/** Hold the auto-dim off while a screen previews the backlight (the slider); the idle time restarts when it is released. */
void ui_idle_inhibit(bool on);
/** Remember a changed UI setting (RAM now, the card a moment later: settings_store.h). */
void ui_state_persist(void);

/** Create the persistent frame (status bar + content area) on @p scr. */
void ui_frame_init(lv_obj_t *scr);
/** Replace the status bar title (screens whose title changes, e.g. the file browser). */
void ui_frame_set_title(const char *t);
/** The status bar's left context slot for @p s: the clock on Home, "n of m" on Now Playing, a count on lists, the path in Files ("" = empty).
 *  The text is kept with the screen and shown while it is on top; cheap to call every tick (nothing is touched when it is unchanged). */
void ui_screen_set_left(ui_screen_t *s, const char *text);

/* ---- Toast strip (top, under the status bar) and HUD strip (bottom), ui_strips.c ----------------------------------------------------------- */

typedef enum { UI_TOAST_WARN, UI_TOAST_INFO } ui_toast_kind_t;
#define UI_TOAST_MS 3000u /* default time a toast stays */
/** Slide a one-line notice in under the status bar: WARN is amber (a problem), INFO soft blue. A new toast replaces the one showing. @p ms 0 = UI_TOAST_MS. */
void ui_toast_show(const char *text, ui_toast_kind_t kind, uint32_t ms);
void ui_toast_hide(void);
/** Bottom strip with a label and a bar (@p pct 0..100, -1 = label only), for a value changing from any screen (volume): 120 ms in, @p ms hold (0 = 1200), 200 ms out. */
void ui_hud_show(const char *label, int pct, uint32_t ms);
void ui_hud_hide(void);
/** Finish the transitions now (a toast, HUD or screen slide in flight jumps to its end state): the host snapshots and the layout audit look at settled screens. */
void ui_frame_settle(void);
/** Allocate a zeroed screen object of @p size bytes (first member must be ui_screen_t) with its root. */
ui_screen_t *ui_screen_new(size_t size, const char *title);
/** Suspend the top screen now (frees its rebuildable objects); a modal calls this BEFORE building itself so the pool peak is lower. */
void ui_frame_suspend_top(void);
/** Undo ui_frame_suspend_top() when the modal could not be built. */
void ui_frame_resume_top(void);
/** Push @p s on the screen stack (it is then owned by the frame). False when it could not be: @p s was NULL (out of memory) or the stack is full (UI_STACK_MAX);
 *  a full stack destroys @p s. A caller that has no other way to say so shows a toast. */
bool ui_frame_push(ui_screen_t *s);
void ui_frame_pop(void);
/** Route one input event (wheel counts are accelerated here). */
void ui_frame_input(const input_event_t *ev, uint32_t now_ms);
/** Periodic work: status bar, screen tick. Called on every UI pass; screens rate-limit themselves. */
void ui_frame_tick(uint32_t now_ms);
/** A selection moved: play the click feedback (if enabled and available). */
void ui_feedback_step(void);
int ui_frame_depth(void);
/** Title of the screen on top (the one that gets the input), "" if none. */
const char *ui_frame_top_title(void);
/** Console line for a screen push / pop (ui.c): "ui: screen 'T' push|pop ..."; @p now_top is the screen revealed by a pop. */
void ui_log_screen(const char *title, const char *what, const char *now_top);

/** Bars (1..4) of the joined Wi-Fi network as the status bar shows them (hysteresis applied); 0 when not joined. */
int ui_wifi_bars(void);

/* ---- List widget (ui_list.c) ---------------------------------------------------------------------- */

typedef struct {
    char label[96];
    char detail[32]; /* right-aligned, optional */
    char sub[48];    /* second line of a two-line list (ui_list_model_t.two_line): Small, ink2; "" = none */
    bool chevron;    /* the Lucide chevron at the right edge */
    const char *icon; /* a Lucide glyph (ui_icons.h, a string constant) before the label; NULL = none */
    bool dim;        /* greyed, not meant to be opened */
    uint8_t bars;    /* signal meter at the right edge, 1..4 (ui_signal_bars; 0 = none) */
    bool mono;       /* the meter is the monochrome three-bar one of the Bluetooth lists (ink and line, never traffic-light colours): bars 1..4 still, shown as 1..3 */
    bool busy;       /* pulsing dots at the right edge instead of the meter: the row's action is still running.
                        The model sets it for as long as the work lasts and clears it on completion; a row driven by
                        an action the screen started (a Networks join) also gives up after UI_BUSY_TIMEOUT_MS, while
                        status rows (scanning, JOINING/NO_IP, radio starting) simply track the HAL state.
                        The list animates it by itself while any bound row is busy. Call ui_list_refresh when it changes. */
} ui_list_item_t;

typedef struct {
    int32_t count;
    void *ctx;
    void (*get)(void *ctx, int32_t idx, ui_list_item_t *out);
    int32_t top;   /* y of the list in its screen (an album's header sits above it; 0 = the whole content area) */
    int32_t bottom; /* pixels kept free below the list (the Bluetooth scan strip); the window is a whole number of rows of what is left */
    bool two_line; /* rows are UIM->row2_h high with a second line (Songs and the song lists): 5 rows instead of 7 */
} ui_list_model_t;

typedef struct ui_list ui_list_t;

/** A virtualised list: only the visible rows exist as objects, whatever the item count. */
ui_list_t *ui_list_create(lv_obj_t *parent, int32_t w, const ui_list_model_t *model);
void ui_list_destroy(ui_list_t *l);
/** Move the selection; plays click feedback if it changed. True if it changed. */
bool ui_list_move(ui_list_t *l, int32_t steps);
int32_t ui_list_selected(const ui_list_t *l);
void ui_list_set_selected(ui_list_t *l, int32_t sel);
/** The model changed (count or contents): rebind the rows. */
void ui_list_refresh(ui_list_t *l);
void ui_list_set_count(ui_list_t *l, int32_t count);
/** Highlight style off (a pane that does not currently have focus). */
void ui_list_set_focus(ui_list_t *l, bool focus);
/** Free the row objects while another screen covers this one (modals); resume rebuilds them. */
void ui_list_suspend(ui_list_t *l);
void ui_list_resume(ui_list_t *l);

/* ---- Full-pane states (ui_pane.c) ------------------------------------------------------------------------------------------------ */

/** A state that fills the content area instead of a list row (design review 3.3): a heading, up to two Small lines, an optional bar with a caption,
 *  an optional accent button (what Select does) and a hint line at the bottom. Hidden parts take no room; the block is centred. */
typedef struct ui_pane ui_pane_t;
#define UI_PANE_BAR_NONE (-1)
#define UI_PANE_BAR_SWEEP (-2) /* a segment travelling along the track: the total is not known yet */
ui_pane_t *ui_pane_create(lv_obj_t *parent);
void ui_pane_destroy(ui_pane_t *p);
/** Texts are raw (file names): NULL or "" hides that part. */
void ui_pane_set(ui_pane_t *p, const char *heading, const char *l1, const char *l2, const char *button, const char *hint);
/** The bar: @p pct 0..100, UI_PANE_BAR_SWEEP, or UI_PANE_BAR_NONE (hidden); @p caption sits under it (NULL = none). */
void ui_pane_set_bar(ui_pane_t *p, int32_t pct, const char *caption);
/** Shows / hides every part (a list shows instead). */
void ui_pane_show(ui_pane_t *p, bool on);
/** Moves the sweeping segment (about 8 steps a second; cheap to call every pass). */
void ui_pane_tick(ui_pane_t *p, uint32_t now_ms);

/* ---- Screens (ui_screens.c, ui_files.c, ui_settings.c, ui_bluetooth.c) ------------------------------------------- */

ui_screen_t *ui_screen_home_create(void);
ui_screen_t *ui_screen_music_create(void);
ui_screen_t *ui_screen_placeholder_create(const char *title, const char *line1, const char *line2);
ui_screen_t *ui_screen_nowplaying_create(void); /* ui_nowplaying.c */
ui_screen_t *ui_screen_nowplaying_page2_create(void); /* ui_nowplaying.c: Select on Now Playing */
ui_screen_t *ui_screen_testtone_create(void);   /* ui_audio.c */
ui_screen_t *ui_screen_extras_create(void);
ui_screen_t *ui_screen_files_create(void);
ui_screen_t *ui_screen_settings_create(void);
ui_screen_t *ui_screen_bluetooth_create(void); /* ui_bluetooth.c */
ui_screen_t *ui_screen_licences_create(void);  /* ui_licences.c: Settings > About > Licences */
ui_screen_t *ui_screen_playlists_create(void); /* ui_playlists.c: Music > Playlists (PLS_AVAILABLE builds) */
/** A yes / no question (ui_bluetooth.c): @p head and @p sub are raw text, Select runs @p yes(ctx) once the question has closed, Menu = no. */
ui_screen_t *ui_confirm_create(const char *title, const char *head, const char *sub, const char *yes_word, const char *no_word, void (*yes)(void *), void *ctx);
ui_screen_t *ui_screen_bt_devices_create(void); /* ui_bluetooth.c: starts a scan when it opens */
/** "Name\nState" of the remembered Bluetooth headset ("Connected", "Pairing...", "Failed: not found", "Not connected"); "" if none (ui_bluetooth.c). */
void ui_bt_headset_text(char *out, size_t n);
ui_screen_t *ui_screen_wifi_create(void); /* ui_wifi.c */
ui_screen_t *ui_screen_wifi_networks_create(void); /* ui_wifi.c: starts a scan when it opens */
ui_screen_t *ui_screen_nettest_create(void); /* ui_nettest.c */
ui_screen_t *ui_screen_inputdemo_create(void); /* input/ui_input_demo.c */

/** True while the library builds with its screen left (Menu on the progress pane: "run in background"); also takes the answer and toasts the outcome. Polled every UI pass
 *  by the frame, which shows three busy dots in the status bar's left slot while it is true (ui_library.c). */
bool ui_library_background(void);

/** Small helpers shared by the screens. */
lv_obj_t *ui_artwork(lv_obj_t *parent, int32_t x, int32_t y, int32_t size);

/** The art box (ui_art.c): the placeholder with a thumbnail from the art service on top. The thumbnail is not copied (it must stay valid while shown:
 *  the art service's contract, art_service.h) and is drawn scaled down into the box. */
typedef struct ui_artbox ui_artbox_t;
ui_artbox_t *ui_artbox_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t size);
/** Show the thumbnail @p px (@p w x @p h RGB565, generation @p gen: the same generation twice does nothing); NULL px = the placeholder. */
void ui_artbox_show(ui_artbox_t *a, const uint16_t *px, uint16_t w, uint16_t h, uint32_t gen);
void ui_artbox_destroy(ui_artbox_t *a);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t colour);
/** Like ui_label() in the Small font (secondary lines, times, pills, hints). */
lv_obj_t *ui_label_small(lv_obj_t *parent, const char *text, uint32_t colour);
/** A Lucide icon (ui_icons.h): a label drawn with the icon font, @p large picks the bigger size (24 / 36 instead of 16 / 24); @p colour is a token or literal. */
lv_obj_t *ui_icon(lv_obj_t *parent, const char *glyph, bool large, uint32_t colour);
/** Set a label's text from a raw string (file name, tag, device name, SSID ...): runs ui_text_display() first, so characters the fonts lack
 *  never draw as an empty box, and does nothing when the label already shows the result. Every label that can carry such a string
 *  is set through this (or ui_label()); the stored string is untouched. */
void ui_label_set_text(lv_obj_t *l, const char *raw);
/** Change a label's colour (from a colour it was created with / last set to). Shared styles: no per-object style memory. */
void ui_label_recolour(lv_obj_t *l, uint32_t from, uint32_t to);
/** Ticker (owner decision, docs/ui.md "Text that does not fit"): @p on makes a one-line label scroll round when its text is wider than its box
 *  (1 s pause, then 35 px/s, x1.5 on 480x360, a gap between repeats); off gives it dots again and rewinds it. The label's box must be one line high. */
void ui_label_ticker(lv_obj_t *l, bool on);
/** Tickers stand still (rewound) while the screen is off. */
void ui_ticker_set_active(bool active);
/** Rewind every ticker to its start with its pause (tours: a snapshot is always of the start position). */
void ui_ticker_restart(void);
void ui_format_size(uint64_t bytes, char *out, size_t n);

#endif // NEUPOD_UI_INTERNAL_H
