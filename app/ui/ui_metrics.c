// SPDX-License-Identifier: MPL-2.0
// UI metrics profiles for hosted builds (NEUPOD_UI_HIRES; see ui_metrics.h). The 320x240 values (ui_metrics_320.h) are the ones
// the UI had before profiles existed and must not change (the host snapshots at 320x240 are compared byte for byte). The
// 480x360 profile is every length scaled by 3/2 through S(), except fonts, which step to the next size, and row counts,
// which stay. The product build has only the 320x240 profile, defined in the header; this file is empty there.
#include "ui_metrics.h"

#ifdef NEUPOD_UI_HIRES
#include "ui_metrics_320.h"

#define k_320x240 k_ui_metrics_320

static const ui_metrics_t *g_active = &k_320x240;

#define S(v) ((v) * 3 / 2)

static const lv_point_precise_t k_diag_a_480[2] = {{S(7), S(7)}, {S(25), S(25)}};
static const lv_point_precise_t k_diag_b_480[2] = {{S(25), S(7)}, {S(7), S(25)}};

static const int32_t k_meter_h_480[UI_METER_BARS] = {S(5), S(8), S(11), S(14)};
static const int32_t k_home_line_y_480[3] = {S(140), S(162), S(182)};
static const int32_t k_dt_x_480[5] = {S(40), S(82), S(124), S(190), S(232)};

static const ui_metrics_t k_480x360 = {
    .name = "480x360",
    .w = 480,
    .h = 360,
    .status_h = S(24),
    .content_h = 360 - S(24),
    .row_h = S(30),
    .rows = (360 - S(24)) / S(30),
    .buf_lines = S(24),
    .bar_inset = S(10),
    .title_w = S(160),
    .title_h = S(18),
    .slot_w = S(66),
    .bar_gap = S(5),
    .batt_w = S(20),
    .batt_h = S(10),
    .batt_nub_w = S(2),
    .batt_nub_h = S(4),
    .font_body = &neupod_font_20,
    .font_large = &neupod_font_28,
    .font_huge = &neupod_font_48,
    .font_small = &neupod_font_16,
    .font_icon_sm = &neupod_icons_24,
    .font_icon_lg = &neupod_icons_36,
    .pad = S(10),
    .line_h = S(18),
    .row_inset = S(10),
    .chev_w = S(28),
    .glyph_bar_w = S(4),
    .glyph_step = S(6),
    .glyph_gap = S(6),
    .meter_h = k_meter_h_480,
    .detail_char_w = S(8),
    .detail_pad = S(10),
    .sb_w = 3,
    .sb_min_h = S(14),
    .sb_radius = 2,
    .sb_gap = 2,
    .sel_inset = S(3),
    .sel_radius = S(4),
    .sel_inset_y = 1,
    .ticker_px_s = S(35),
    .sel_anim_ms = 80,
    .icon_w = S(22),
    .art_radius = S(3),
    .art_ring_w = S(3),
    .ph_dy1 = S(-14),
    .ph_dy2 = S(18),
    .ph_margin = S(20),
    .home_list_w = 269, /* 56 % */
    .home_art = 175, /* the pane (211) less 18 each side */
    .home_art_off = S(12),
    .home_line_y = k_home_line_y_480,
    .row2_h = S(40),
    .rows2 = 5,
    .row2_y1 = S(3),
    .row2_y2 = S(21),
    .np_margin = S(10),
    .np_art = S(112),
    .np_art_y = S(12),
    .np_text_x = S(134),
    .np_title_y = S(10),
    .np_title_h = S(26),
    .np_artist_y = S(40),
    .np_album_y = S(60),
    .np_album_h = S(16),
    .np_pill_y = S(102),
    .np_pill_h = S(18),
    .np_pill_pad = S(6),
    .np_pill_gap = S(6),
    .np_bar_y = S(142),
    .np_bar_h = S(4),
    .np_bar_seek_h = S(6),
    .np_knob = S(14),
    .np_time_y = S(150),
    .np_time_h = S(16),
    .np_seek_y = S(66),
    .np_vol_y = S(190),
    .np_vol_x = S(144),
    .np_vol_bar_y = S(196),
    .np_vol_bar_h = S(4),
    .np_hint_y = S(192),
    .hd_art = S(64),
    .hd_art_y = S(8),
    .hd_text_x = S(84),
    .hd_line1_y = S(6),
    .hd_line2_y = S(32),
    .hd_line3_y = S(52),
    .hd_rule_y = S(76),
    .lr_tile = S(44),
    .lr_tile_y = S(14),
    .lr_tile_inset = S(14),
    .lr_tile_radius = S(8),
    .lr_rail_inset = S(4),
    .lr_rail_y = S(6),
    .lr_rail_h = S(204),
    .tt_y = {S(14), S(40), S(60), S(84)},
    .set_list_w = 269, /* 56 % */
    .set_pane_pad = S(6),
    .set_head_y = S(22),
    .set_body_y = S(52),
    .set_body_line_space = S(6),
    .toast_pad = S(4),
    .toast_h = S(30),
    .hud_h = S(26),
    .pill_h = S(24),
    .pill_margin = S(8),
    .pill_pad = S(12),
    .hold_line_h = 4,
    .hud_bar_h = S(4),
    .in_text_value_y = S(30),
    .in_cell_y = S(110),
    .in_cell_w = S(60),
    .in_hint_y = S(180),
    .in_text_hint_x = S(5),
    .in_hint_x = S(12),
    .in_num_value_y = S(60),
    .in_bar_x = S(40),
    .in_bar_y = S(120),
    .in_bar_h = S(14),
    .in_dt_y = S(80),
    .in_dt_x = k_dt_x_480,
    .in_dt_time_shift = S(-70),
    .in_overlay = S(64),
    .in_overlay_radius = S(10),
    .sp_wordmark_y = S(30),
    .sp_track_y = S(100),
    .sp_track_w = S(150),
    .sp_track_h = S(2),
    .sp_shimmer_w = S(44),
    .sp_icon = S(32),
    .sp_icon_gap = S(6),
    .sp_icon_row_y = S(142),
    .sp_icon_radius = S(8),
    .sp_caption_gap = S(8),
    .sp_caption_h = S(18),
    .sp_hold_h = S(2),
    .sp_ver_inset = S(8),
    .sp_ring = S(22),
    .sp_ring_dot = S(7),
    .sp_shape_border = S(2),
    .sp_hap_w = S(9),
    .sp_hap_h = S(16),
    .sp_hap_radius = S(3),
    .sp_hap_near_w = S(2),
    .sp_hap_near_h = S(10),
    .sp_hap_far_h = S(6),
    .sp_hap_near_dx = S(9),
    .sp_hap_far_dx = S(13),
    .sp_cross_under_w = S(7),
    .sp_cross_w = S(3),
    .sp_diag_a = k_diag_a_480,
    .sp_diag_b = k_diag_b_480,
};
#undef S

static const ui_metrics_t *const k_profiles[] = {&k_320x240, &k_480x360};

const ui_metrics_t *const *ui_metrics_profiles(size_t *count) {
    *count = sizeof k_profiles / sizeof k_profiles[0];
    return k_profiles;
}

const ui_metrics_t *ui_metrics_select(uint32_t w, uint32_t h) {
    const ui_metrics_t *best = &k_320x240; /* a smaller panel gets it too */
    for (size_t i = 0; i < sizeof k_profiles / sizeof k_profiles[0]; i++) {
        if (w >= (uint32_t)k_profiles[i]->w && h >= (uint32_t)k_profiles[i]->h && k_profiles[i]->w >= best->w) {
            best = k_profiles[i];
        }
    }
    return best;
}

void ui_metrics_set(const ui_metrics_t *m) { g_active = m; }

const ui_metrics_t *ui_metrics(void) { return g_active; }

#endif // NEUPOD_UI_HIRES
