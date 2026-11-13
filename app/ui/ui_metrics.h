// SPDX-License-Identifier: MPL-2.0
/*
 * UI metrics: every pixel size the LVGL UI uses, in one struct chosen once at UI init from the panel size
 * (docs/ui.md "Resolution profiles", ADR-0018). The product panel is 320x240 (the profile that holds the
 * exact numbers the UI always had); hosted builds (NEUPOD_UI_HIRES: the host simulator and the Y2 testbed)
 * also carry a 480x360 profile at 1.5x with the next-size fonts (app/ui/fonts). Screens read their geometry from
 * ui_metrics(); nothing outside ui_metrics.c multiplies by a scale factor.
 */
#ifndef NEUPOD_UI_METRICS_H
#define NEUPOD_UI_METRICS_H

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "fonts/neupod_fonts.h"

/* Compile-time ceilings for the things that are sized statically (LVGL partial-render buffers, list row pool). */
#ifdef NEUPOD_UI_HIRES
#define UI_BUF_W_MAX 480
#define UI_BUF_LINES_MAX 36
#else
#define UI_BUF_W_MAX 320
#define UI_BUF_LINES_MAX 24
#endif
#define UI_ROWS_MAX 7 /* rows_visible of every profile (checked by tests/test_ui_metrics.c) */

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
    int32_t bar_inset; /* status bar: left slot and the right-hand group inset from the edges */
    int32_t title_w, title_h;
    int32_t slot_w;    /* status bar: widest left slot text (clock, "3 of 12", count, path) */
    int32_t bar_gap;   /* status bar: gap between the right-hand items (Wi-Fi, Bluetooth, percentage, battery) */
    int32_t batt_w, batt_h, batt_nub_w, batt_nub_h; /* the drawn battery: body outline, positive terminal */

    /* Text */
    const lv_font_t *font_body;  /* every label unless a screen asks for more (Montserrat 14 / 20, Latin-1) */
    const lv_font_t *font_large; /* titles, values, splash icons (20 / 28) */
    const lv_font_t *font_huge;  /* splash wordmark, the art-less note glyph (48 / 48: ASCII only) */
    const lv_font_t *font_small; /* secondary lines, times, pills, hints, the status bar's left slot (12 / 16) */
    const lv_font_t *font_icon_sm; /* Lucide icons for rows and the status bar (16 / 24; ui_icons.h) */
    const lv_font_t *font_icon_lg; /* Lucide icons for larger spots (24 / 36) */
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
    int32_t sel_inset, sel_radius;             /* selection bar: inset from the list's left and right edges, corner radius */
    int32_t sel_inset_y;                       /* ... and from the top and bottom of its row */
    int32_t ticker_px_s;                       /* overlong text scrolls at this speed (after a 1 s pause) */
    int32_t sel_anim_ms;                       /* the bar slides to an adjacent row in this time (0 = jumps) */
    int32_t icon_w;                            /* width reserved for a row's leading icon (folder, file) */

    /* Shared widgets */
    int32_t art_radius, art_ring_w; /* artwork placeholder */

    /* Placeholder screens (Podcasts, Photos, Shuffle) */
    int32_t ph_dy1, ph_dy2, ph_margin;

    /* Home: list on the left, art / status pane on the right */
    int32_t home_list_w;
    int32_t home_art, home_art_off;
    int32_t home_title_y, home_artist_y, home_bar_y, home_bar_h, home_time_y; /* playing: title, artist, 3 px progress bar, elapsed / remaining */
    int32_t home_clock_y, home_date_y, home_count_y;                        /* idle: the 48 px clock, the date, "1,204 songs . 72 %" */
    const int32_t *home_line_y; /* 3 text line y positions */

    /* Two-line list rows (Songs and the song lists, docs/ui.md "Library lists"): title in Body, "Artist . Album" in Small underneath */
    int32_t row2_h;           /* row height (40 / 60) */
    int32_t rows2;            /* rows that fit the content area (5) */
    int32_t row2_y1, row2_y2; /* top of the title line, top of the second line */

    /* Now Playing (page 1): art, then Title / Artist / Album . Year, the pill row, the progress bar with its times, the volume strip */
    int32_t np_margin, np_art, np_art_y, np_text_x, np_title_y, np_title_h, np_artist_y, np_album_y, np_album_h;
    int32_t np_pill_y, np_pill_h, np_pill_pad, np_pill_gap;
    int32_t np_bar_y, np_bar_h, np_bar_seek_h, np_knob, np_time_y, np_time_h;
    int32_t np_seek_y; /* the big seek time (seek mode) */
    int32_t np_vol_y, np_vol_x, np_vol_bar_y, np_vol_bar_h, np_hint_y;
    /* Now Playing page 2 and the album header: a 64 / 96 px art, text to its right */
    int32_t hd_art, hd_art_y, hd_text_x, hd_line1_y, hd_line2_y, hd_line3_y, hd_rule_y;

    /* Fast-scroll letter overlay: a tile at the top right and the index rail down the right edge (every second letter) */
    int32_t lr_tile, lr_tile_y, lr_tile_inset, lr_tile_radius, lr_rail_inset, lr_rail_y, lr_rail_h;

    /* Cover Flow (Albums list, flat five tiles; docs/ui.md "Cover Flow"): tile sizes by distance from the centre (a sixth, hidden size lets a tile slide in from the
     * edge), the tile row's top, the distance of each distance's tile centre from the screen centre, then title, subtitle and hint lines */
    int32_t cf_c, cf_n, cf_o, cf_e, cf_y, cf_dx1, cf_dx2, cf_dx3, cf_title_y, cf_sub_y, cf_hint_y;

    /* Test tone: labels below the 3-row list */
    int32_t tt_y[4];

    /* Settings: list left, summary pane right */
    int32_t set_list_w;
    int32_t set_pane_pad, set_head_y, set_body_y, set_body_line_space;

    /* Toast strip (top, under the status bar), HUD strip (bottom), Hold pill and the Power-hold line (docs/ui.md "Design system") */
    int32_t toast_pad;
    int32_t toast_h, hud_h;
    int32_t pill_h, pill_margin, pill_pad; /* the lock pill: height, distance from the bottom edge, text inset */
    int32_t hold_line_h;                   /* the Power-hold progress line under the status bar */
    int32_t hud_bar_h;

    /* Full-pane states (ui_pane.c: library building / empty / error, the Update Library result) */
    int32_t st_bar_w, st_bar_h, st_btn_h, st_btn_pad, st_gap, st_hint_inset;

    /* Input widgets (ui_input.c, design review 3.7): the text field box, the ring of nine glyphs (cell width by distance from the current one), the mode chips,
     * the hint; number / slider (the Big value, the bar, min / max under its ends); date / time (five fields, each with its name under it) */
    int32_t in_field_y, in_field_h, in_field_radius;
    int32_t in_ring_y, in_cw0, in_cw1, in_cw2, in_cw3, in_cw4;
    int32_t in_chip_y, in_chip_h, in_chip_pad, in_chip_gap;
    int32_t in_hint_y, in_hint_x;
    int32_t in_num_value_y, in_bar_x, in_bar_y, in_bar_h, in_bar_label_y;
    int32_t in_dt_y, in_dt_name_y;
    int32_t in_dt_time_shift; /* a time-only picker moves its two fields by the shift */
    const int32_t *in_dt_x;   /* 5 field x positions */

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
