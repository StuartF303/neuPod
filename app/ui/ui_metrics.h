// SPDX-License-Identifier: MPL-2.0
/*
 * UI metrics: every pixel size the LVGL UI uses, in one struct chosen once at UI init from the panel size
 * (docs/ui.md "Resolution profiles", ADR-0018). The product panel is 320x240 (the profile that holds the
 * exact numbers the UI always had); hosted builds (NEUPOD_UI_HIRES: the host simulator and the Y2 testbed)
 * also carry a 480x360 profile at 1.5x with the next-size Montserrat fonts. Screens read their geometry from
 * ui_metrics(); nothing outside ui_metrics.c multiplies by a scale factor.
 */
#ifndef NEUPOD_UI_METRICS_H
#define NEUPOD_UI_METRICS_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/* Compile-time ceilings for the things that are sized statically (LVGL partial-render buffers, list row pool). */
#ifdef NEUPOD_UI_HIRES
#define UI_BUF_W_MAX 480
#define UI_BUF_LINES_MAX 36
#else
#define UI_BUF_W_MAX 320
#define UI_BUF_LINES_MAX 24
#endif
#define UI_ROWS_MAX 9 /* rows_visible of every profile (checked by tests/test_ui_metrics.c) */

#define UI_METER_BARS 4 /* signal meter / busy dots at the right edge of a row */

typedef struct {
    const char *name;
    int32_t w, h;

    /* Frame: status bar on top, content area below it. */
    int32_t status_h;
    int32_t content_h; /* h - status_h */
    int32_t row_h;     /* list row height */
    int32_t rows;      /* list rows that fit the content area (content_h / row_h) */
    int32_t buf_lines; /* LVGL partial-render buffer height */
    int32_t bar_inset; /* status bar: play glyph and battery inset from the edges */
    int32_t wifi_x;    /* status bar: Wi-Fi glyph x */
    int32_t bt_x;      /* status bar: Bluetooth glyph x */
    int32_t title_w, title_h;

    /* Text */
    const lv_font_t *font_body;  /* every label unless a screen asks for more (Montserrat 14 / 20) */
    const lv_font_t *font_large; /* titles, values, splash icons (20 / 28) */
    const lv_font_t *font_huge;  /* splash wordmark, the art-less note glyph (48 / 48: the largest built-in) */
    int32_t pad;                 /* left text inset of rows, side margin of free-standing text */
    int32_t line_h;              /* fixed height of a one-line label (so "..." replaces wrapping) */

    /* List rows */
    int32_t row_inset;    /* right inset of the rightmost item */
    int32_t chev_w;       /* right inset when the row has a chevron */
    int32_t glyph_bar_w;  /* one rectangle of the signal meter / busy dots (dots are as tall as they are wide) */
    int32_t glyph_step;   /* distance between rectangles */
    int32_t glyph_gap;    /* gap between the glyph and the detail text */
    /* Arrays that are indexed at run time are pointers to separate tables: in the product build the struct is a header constant
     * that every file would otherwise get its own copy of the moment one of them reads an element at a variable index. */
    const int32_t *meter_h; /* UI_METER_BARS bar heights */
    int32_t detail_char_w; /* estimated width of one detail character */
    int32_t detail_pad;
    int32_t sb_w, sb_min_h, sb_radius, sb_gap; /* scroll bar; gap = distance from the right edge */

    /* Shared widgets */
    int32_t art_radius, art_ring_w; /* artwork placeholder */

    /* Placeholder screens (Podcasts, Photos, Shuffle) */
    int32_t ph_dy1, ph_dy2, ph_margin;

    /* Home: list on the left, art / status pane on the right */
    int32_t home_list_w;
    int32_t home_art, home_art_off;
    const int32_t *home_line_y; /* 3 text line y positions */

    /* Now Playing */
    int32_t np_margin, np_art, np_text_x, np_title_y, np_title_h, np_artist_y, np_album_y, np_badge_y, np_state_y, np_state_h;
    int32_t np_bar_y, np_bar_h, np_time_y, np_modes_dx, np_vol_y, np_vol_bar_y, np_vol_bar_h;

    /* Test tone: labels below the 3-row list */
    int32_t tt_y[4];

    /* Settings: list left, summary pane right */
    int32_t set_list_w;
    int32_t set_pane_pad, set_head_y, set_body_y, set_body_line_space;

    /* Files */
    int32_t toast_pad;

    /* Input widgets (ui_input.c) */
    int32_t in_text_value_y, in_cell_y, in_cell_w, in_hint_y, in_text_hint_x, in_hint_x;
    int32_t in_num_value_y, in_bar_x, in_bar_y, in_bar_h;
    int32_t in_dt_y, in_dt_time_shift; /* a time-only picker moves its two fields by the shift */
    const int32_t *in_dt_x;            /* 5 field x positions */
    int32_t in_overlay, in_overlay_radius;

    /* Splash */
    int32_t sp_wordmark_y, sp_track_y, sp_track_w, sp_track_h, sp_shimmer_w;
    int32_t sp_icon, sp_icon_gap, sp_icon_row_y, sp_icon_radius;
    int32_t sp_caption_gap, sp_caption_h, sp_hold_h, sp_ver_inset;
    int32_t sp_ring, sp_ring_dot, sp_shape_border;
    int32_t sp_hap_w, sp_hap_h, sp_hap_radius, sp_hap_near_w, sp_hap_near_h, sp_hap_far_h, sp_hap_near_dx, sp_hap_far_dx;
    int32_t sp_cross_under_w, sp_cross_w;
    const lv_point_precise_t *sp_diag_a, *sp_diag_b; /* the cross of a degraded icon (2 points each) */
} ui_metrics_t;

#ifdef NEUPOD_UI_HIRES
/** Pick the profile for a panel of @p w x @p h: the largest compiled-in one that fits (320x240 when none does). */
const ui_metrics_t *ui_metrics_select(uint32_t w, uint32_t h);
/** Make @p m the active profile (ui_task does this once, before anything is drawn). */
void ui_metrics_set(const ui_metrics_t *m);
/** The active profile (320x240 until set). */
const ui_metrics_t *ui_metrics(void);
/** The compiled-in profiles, for tests. */
const ui_metrics_t *const *ui_metrics_profiles(size_t *count);
#else
/* Product build: one profile, a compile-time constant (every field read folds away; no run-time selection, no extra RAM). */
#include "ui_metrics_320.h"
static inline const ui_metrics_t *ui_metrics(void) { return &k_ui_metrics_320; }
static inline const ui_metrics_t *ui_metrics_select(uint32_t w, uint32_t h) {
    (void)w;
    (void)h;
    return &k_ui_metrics_320;
}
static inline void ui_metrics_set(const ui_metrics_t *m) { (void)m; }
static inline const ui_metrics_t *const *ui_metrics_profiles(size_t *count) {
    static const ui_metrics_t *const k_one[] = {&k_ui_metrics_320};
    *count = 1;
    return k_one;
}
#endif

#endif // NEUPOD_UI_METRICS_H
