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
#include "ui_logic.h"
#include "ui_metrics.h"

/* Geometry comes from the active metrics profile (ui_metrics.h), chosen at UI init from the panel size. */
#define UIM (ui_metrics())
#define UI_W (UIM->w)
#define UI_H (UIM->h)

// Boot splash palette: deep blue-black ground, one cyan accent, red only for "not working".
#define UI_COL_BG 0x0A1020
#define UI_COL_BG2 0x16263F
#define UI_COL_BAR 0x1B2A3F
#define UI_COL_ACCENT 0x2EC4F1
#define UI_COL_TEXT 0xE8EEF5
#define UI_COL_DIM 0x6C7B8F
#define UI_COL_ERR 0xFF4D4F

// Menu palette (docs/ui.md mock-ups): light surfaces, blue selection bar.
#define UI_L_BG 0xFFFFFF
#define UI_L_PANE 0xF4F4F1
#define UI_L_BAR_TOP 0xF7F7F5
#define UI_L_BAR_BOT 0xD9D9D4
#define UI_L_LINE 0xB9B9B3
#define UI_L_TEXT 0x1D1D1B
#define UI_L_DIM 0x5B5A56
#define UI_L_FAINT 0x8F8F89
#define UI_L_SEL_TOP 0x5F9DF0
#define UI_L_SEL_BOT 0x2663C6
#define UI_L_OK 0x3FA34D
#define UI_L_WARN 0xC77700
#define UI_L_ERR 0xC62828

/* Font Awesome glyphs with no LV_SYMBOL_*, in the UI fonts (tools/gen_ui_fonts.sh; keep ui_text_has_glyph() in step). */
#define UI_SYM_POWER "\xEF\x80\x91"  /* U+F011 */
#define UI_SYM_LOCK "\xEF\x80\xA3"   /* U+F023 */
#define UI_SYM_UNLOCK "\xEF\x82\x9C" /* U+F09C */

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
} ui_state_t;
extern ui_state_t g_ui_state;
/** Show the backlight level on the panel: the set level, or the dimmed one while idle. The only place that computes it. */
void ui_apply_backlight(void);
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
/** Allocate a zeroed screen object of @p size bytes (first member must be ui_screen_t) with its root. */
ui_screen_t *ui_screen_new(size_t size, const char *title);
/** Suspend the top screen now (frees its rebuildable objects); a modal calls this BEFORE building itself so the pool peak is lower. */
void ui_frame_suspend_top(void);
/** Undo ui_frame_suspend_top() when the modal could not be built. */
void ui_frame_resume_top(void);
void ui_frame_push(ui_screen_t *s);
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
/** Settings pane lines for an open Bluetooth audio stream ("SBC-XQ bitpool 76\n455 kbit/s\n0 underruns"); "" when there is none. */
void ui_bt_stream_pane_text(char *out, size_t n);

/* ---- List widget (ui_list.c) ---------------------------------------------------------------------- */

typedef struct {
    char label[96];
    char detail[32]; /* right-aligned, optional */
    bool chevron;    /* "›" at the right edge */
    bool dim;        /* greyed, not meant to be opened */
    uint8_t bars;    /* signal meter at the right edge, 1..4 (ui_signal_bars; 0 = none) */
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

/* ---- Screens (ui_screens.c, ui_files.c, ui_settings.c, ui_bluetooth.c) ------------------------------------------- */

ui_screen_t *ui_screen_home_create(void);
ui_screen_t *ui_screen_music_create(void);
ui_screen_t *ui_screen_placeholder_create(const char *title, const char *line1, const char *line2);
ui_screen_t *ui_screen_nowplaying_create(void); /* ui_audio.c */
ui_screen_t *ui_screen_testtone_create(void);   /* ui_audio.c */
ui_screen_t *ui_screen_extras_create(void);
ui_screen_t *ui_screen_files_create(void);
ui_screen_t *ui_screen_settings_create(void);
ui_screen_t *ui_screen_bluetooth_create(void); /* ui_bluetooth.c */
ui_screen_t *ui_screen_licences_create(void);  /* ui_licences.c: Settings > About > Licences */
ui_screen_t *ui_screen_bt_devices_create(void); /* ui_bluetooth.c: starts a scan when it opens */
/** "Name\nState" of the remembered Bluetooth headset ("Connected", "Pairing...", "Failed: not found", "Not connected"); "" if none (ui_bluetooth.c). */
void ui_bt_headset_text(char *out, size_t n);
ui_screen_t *ui_screen_wifi_create(void); /* ui_wifi.c */
ui_screen_t *ui_screen_wifi_networks_create(void); /* ui_wifi.c: starts a scan when it opens */
ui_screen_t *ui_screen_nettest_create(void); /* ui_nettest.c */
ui_screen_t *ui_screen_inputdemo_create(void); /* input/ui_input_demo.c */

/** Small helpers shared by the screens. */
lv_obj_t *ui_artwork(lv_obj_t *parent, int32_t x, int32_t y, int32_t size);
lv_obj_t *ui_label(lv_obj_t *parent, const char *text, uint32_t colour);
/** Set a label's text from a raw string (file name, tag, device name, SSID ...): runs ui_text_display() first, so characters the fonts lack
 *  never draw as an empty box, and does nothing when the label already shows the result. Every label that can carry such a string
 *  is set through this (or ui_label()); the stored string is untouched. */
void ui_label_set_text(lv_obj_t *l, const char *raw);
/** Change a label's colour (from a colour it was created with / last set to). Shared styles: no per-object style memory. */
void ui_label_recolour(lv_obj_t *l, uint32_t from, uint32_t to);
void ui_format_size(uint64_t bytes, char *out, size_t n);

#endif // NEUPOD_UI_INTERNAL_H
