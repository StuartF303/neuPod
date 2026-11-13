// SPDX-License-Identifier: MPL-2.0
// Layout invariants of every UI metrics profile (app/ui/ui_metrics.c): the numbers are static, so "does the layout fit the panel"
// can be checked without drawing. The 320x240 profile is also pinned to the design system's metric table (docs/ui.md, review section 2.3):
// row 30, 7 rows, status bar 24, padding 10, selection inset 3 / radius 4 ... Runs on every profile compiled in.
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "boot_briefs.h"
#include "lvgl.h"
#include "fonts/neupod_fonts.h"
#include "src/misc/lv_text_private.h" /* lv_text_get_width: LVGL private API, fine for a host test */
#include "ui_logic.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "unit.h"

#define CAPS 8  /* capabilities.h CAP_COUNT: subsystem icons on the splash */

void neupod_panic_assert(const char *file, int line) {
    (void)file;
    (void)line;
    abort(); // LVGL's assert hook; not expected to fire
}

static int32_t lh(const lv_font_t *f) { return lv_font_get_line_height(f); }

TEST(profile_320_is_the_design_system_layout) {
    const ui_metrics_t *m = ui_metrics_select(320, 240);
    ASSERT_TRUE(strcmp(m->name, "320x240") == 0);
    ASSERT_EQ(320, m->w);
    ASSERT_EQ(240, m->h);
    ASSERT_EQ(24, m->status_h);
    ASSERT_EQ(30, m->row_h);
    ASSERT_EQ(7, m->rows);
    ASSERT_EQ(216, m->content_h);
    ASSERT_EQ(10, m->pad);
    ASSERT_EQ(3, m->sel_inset);
    ASSERT_EQ(4, m->sel_radius);
    ASSERT_EQ(30, m->toast_h);
    ASSERT_EQ(26, m->hud_h);
    ASSERT_EQ(2, m->sb_w);
    ASSERT_EQ(1, m->sb_gap);
    ASSERT_TRUE(m->font_small == &neupod_font_12);
    ASSERT_TRUE(m->font_icon_sm == &neupod_icons_16);
    ASSERT_TRUE(m->font_icon_lg == &neupod_icons_24);
    ASSERT_EQ(24, m->buf_lines);
    ASSERT_TRUE(m->font_body == &neupod_font_14);
    ASSERT_TRUE(m->font_large == &neupod_font_20);
    ASSERT_TRUE(m->font_huge == &neupod_font_48);
    ASSERT_EQ(112, m->np_art);
    ASSERT_EQ(117, m->home_art); /* the pane (141) less 12 each side */
    ASSERT_EQ(179, m->home_list_w); /* 56 % */
    ASSERT_EQ(179, m->set_list_w);
    ASSERT_EQ(32, m->sp_icon);
    ASSERT_EQ(142, m->sp_icon_row_y);
}

TEST(every_profile_fits_its_panel) {
    size_t n;
    const ui_metrics_t *const *p = ui_metrics_profiles(&n);
    ASSERT_TRUE(n >= 1);
    for (size_t i = 0; i < n; i++) {
        const ui_metrics_t *m = p[i];
        printf("  profile %s\n", m->name);
        // frame
        ASSERT_EQ(m->h - m->status_h, m->content_h);
        ASSERT_EQ(m->content_h / m->row_h, m->rows);
        ASSERT_TRUE(m->rows >= 1 && m->rows <= UI_ROWS_MAX);
        ASSERT_TRUE(m->status_h + m->rows * m->row_h <= m->h); // title bar + rows fit the height
        ASSERT_TRUE(m->w <= UI_BUF_W_MAX && m->buf_lines <= UI_BUF_LINES_MAX); // the static LVGL buffers are big enough
        ASSERT_TRUE(m->title_w <= m->w && m->title_h <= m->status_h);
        // a row holds one line of its font, the status bar holds its title
        ASSERT_TRUE(lh(m->font_body) <= m->row_h);
        ASSERT_TRUE(lh(m->font_body) <= m->line_h + 2);
        ASSERT_TRUE(lh(m->font_large) <= m->status_h);
        // design system: the toast / HUD / pill strips fit under and over the content, the selection bar is taller than the text it carries,
        // the small font is smaller than the body font, the icons fit a row and the status bar, the left slot stays clear of the centred title
        ASSERT_TRUE(m->toast_h + m->status_h < m->h / 4);
        ASSERT_TRUE(m->hud_h < m->content_h / 4 && m->pill_h + 2 * m->pill_margin < m->hud_h * 2);
        ASSERT_TRUE(m->row_h - 2 * m->sel_inset_y > lh(m->font_body));
        ASSERT_TRUE(m->sel_inset * 2 < m->w / 8 && m->sel_radius * 2 < m->row_h);
        ASSERT_TRUE(lh(m->font_small) < lh(m->font_body) && lh(m->font_small) <= m->status_h - 2);
        ASSERT_TRUE(lh(m->font_icon_sm) <= m->row_h && lh(m->font_icon_sm) <= m->status_h && lh(m->font_icon_lg) <= m->content_h);
        ASSERT_TRUE(m->batt_h < m->status_h);
        ASSERT_TRUE(m->title_w + 2 * (m->bar_inset + m->slot_w) <= m->w + 2 * m->pad);
        ASSERT_TRUE(m->hold_line_h < m->status_h / 2);
        ASSERT_TRUE(m->glyph_bar_w * UI_METER_BARS + (UI_METER_BARS - 1) * (m->glyph_step - m->glyph_bar_w) <= m->row_h * 2);
        ASSERT_TRUE(m->meter_h[UI_METER_BARS - 1] <= m->row_h);
        ASSERT_TRUE(m->row_inset + m->chev_w < m->w / 4);
        // Home: list + pane = the screen, the art box and the three text lines are inside the pane
        const int32_t pane_w = m->w - m->home_list_w;
        ASSERT_TRUE(m->home_art_off + m->home_art <= pane_w);
        ASSERT_TRUE(m->home_art_off + m->home_art <= m->home_line_y[0]);
        ASSERT_TRUE(m->home_line_y[0] < m->home_line_y[1] && m->home_line_y[1] < m->home_line_y[2]);
        ASSERT_TRUE(m->home_line_y[2] + lh(m->font_body) <= m->content_h);
        // Now Playing: the art box and the text column sit side by side, the bars fit under them
        ASSERT_TRUE(m->np_margin + m->np_art <= m->np_text_x);
        ASSERT_TRUE(m->np_art_y + m->np_art <= m->np_bar_y); // art box fits above the progress bar
        ASSERT_TRUE(m->np_text_x < m->w - m->np_margin);
        ASSERT_TRUE(m->np_margin + m->np_art < m->np_text_x);
        ASSERT_TRUE(m->np_title_y + lh(m->font_large) <= m->np_artist_y);
        ASSERT_TRUE(m->np_artist_y + lh(m->font_body) <= m->np_album_y);
        ASSERT_TRUE(m->np_album_y + lh(m->font_small) <= m->np_pill_y);
        ASSERT_TRUE(m->np_pill_y + m->np_pill_h <= m->np_art_y + m->np_art); // the pills end with the art
        ASSERT_TRUE(m->np_seek_y + lh(m->font_huge) <= m->np_bar_y);          // the big seek time fits above the bar
        ASSERT_TRUE(m->np_bar_y + m->np_bar_h / 2 - m->np_bar_seek_h / 2 >= m->np_seek_y);
        ASSERT_TRUE(m->np_bar_y + m->np_bar_h / 2 + m->np_knob / 2 <= m->np_time_y); // the knob stays above the times
        ASSERT_TRUE(m->np_bar_y + m->np_bar_h <= m->np_time_y);
        ASSERT_TRUE(m->np_time_y + lh(m->font_small) <= m->np_vol_y);
        ASSERT_TRUE(m->np_vol_y + lh(m->font_small) <= m->content_h);
        ASSERT_TRUE(m->np_hint_y + lh(m->font_small) <= m->content_h);
        ASSERT_TRUE(m->np_vol_bar_y + m->np_vol_bar_h <= m->content_h);
        ASSERT_TRUE(m->np_vol_x > m->np_margin && m->np_vol_x < m->w - m->np_margin);
        // two-line rows, album header, letter rail
        ASSERT_TRUE(m->rows2 >= 1 && m->rows2 * m->row2_h <= m->content_h && m->rows2 <= m->rows);
        ASSERT_TRUE(m->row2_y1 + lh(m->font_body) <= m->row2_y2 && m->row2_y2 + lh(m->font_small) <= m->row2_h);
        ASSERT_TRUE(m->hd_art_y + m->hd_art <= m->hd_rule_y);
        ASSERT_TRUE(m->hd_line3_y + lh(m->font_small) <= m->hd_rule_y);
        ASSERT_TRUE(m->hd_rule_y + 2 + m->row_h <= m->content_h); // at least one row under the header
        ASSERT_TRUE(m->lr_tile + m->lr_tile_y <= m->content_h && m->lr_tile + m->lr_tile_inset < m->w);
        ASSERT_TRUE(m->lr_rail_y + m->lr_rail_h <= m->content_h);
        ASSERT_TRUE(m->lr_rail_h >= 13 * lh(m->font_small)); // every second letter of 26 fits one under the other
        // Cover Flow: sizes fall away from the centre, the visible tiles stay on the panel, the text lines fit below the tiles and above the hint
        ASSERT_TRUE(m->cf_c > m->cf_n && m->cf_n > m->cf_o && m->cf_o > m->cf_e);
        ASSERT_TRUE(m->cf_dx1 > 0 && m->cf_dx1 < m->cf_dx2 && m->cf_dx2 < m->cf_dx3);
        ASSERT_TRUE(m->w / 2 + m->cf_dx2 + m->cf_o / 2 <= m->w); // the outer tile is whole
        ASSERT_TRUE(m->cf_y + m->cf_c <= m->cf_title_y);
        ASSERT_TRUE(m->cf_title_y + lh(m->font_body) <= m->cf_sub_y);
        ASSERT_TRUE(m->cf_sub_y + lh(m->font_small) <= m->cf_hint_y);
        ASSERT_TRUE(m->cf_hint_y + lh(m->font_small) <= m->content_h);
        // Test tone: its labels fit under the three list rows
        ASSERT_TRUE(3 * m->row_h + m->tt_y[3] + 2 * lh(m->font_body) <= m->content_h);
        // Settings: list + pane = the screen
        ASSERT_TRUE(m->set_list_w < m->w && m->w - m->set_list_w > 4 * m->set_pane_pad);
        ASSERT_TRUE(m->set_body_y + 4 * lh(m->font_body) <= m->content_h);
        // Input widgets
        ASSERT_TRUE(m->in_cw0 + 2 * (m->in_cw1 + m->in_cw2 + m->in_cw3 + m->in_cw4) <= m->w - 2 * m->pad); // the nine cells fit
        ASSERT_TRUE(m->in_cw0 > m->in_cw1 && m->in_cw1 > m->in_cw2 && m->in_cw2 > m->in_cw3 && m->in_cw3 > m->in_cw4);
        ASSERT_TRUE(m->in_field_y + m->in_field_h <= m->in_ring_y);
        ASSERT_TRUE(m->in_ring_y + lh(m->font_huge) <= m->in_chip_y);
        ASSERT_TRUE(m->in_chip_y + m->in_chip_h <= m->in_hint_y);
        ASSERT_TRUE(m->in_chip_h >= lh(m->font_small));
        ASSERT_TRUE(m->in_hint_y + lh(m->font_small) <= m->content_h);
        ASSERT_TRUE(m->in_num_value_y + lh(m->font_huge) <= m->in_bar_y);
        ASSERT_TRUE(m->in_bar_y + m->in_bar_h <= m->in_bar_label_y);
        ASSERT_TRUE(m->in_bar_label_y + lh(m->font_small) <= m->in_hint_y);
        ASSERT_TRUE(m->in_bar_x * 2 < m->w);
        ASSERT_TRUE(m->in_dt_x[4] + 2 * lh(m->font_large) <= m->w); // two digits are about one line height wide
        ASSERT_TRUE(m->in_dt_y + lh(m->font_large) <= m->in_dt_name_y);
        ASSERT_TRUE(m->in_dt_name_y + lh(m->font_small) <= m->in_hint_y);
        ASSERT_TRUE(m->in_dt_x[3] + m->in_dt_time_shift >= 0); // time-only: the two time fields move left
        // Splash: icons, caption, version line and the hold bar stack up inside the panel, the icon row is not wider than it
        ASSERT_TRUE(CAPS * m->sp_icon + (CAPS - 1) * m->sp_icon_gap <= m->w);
        ASSERT_TRUE(m->sp_wordmark_y + lh(m->font_huge) <= m->sp_track_y);
        ASSERT_TRUE(m->sp_track_y + m->sp_track_h <= m->sp_icon_row_y);
        ASSERT_TRUE(m->sp_track_w <= m->w && m->sp_shimmer_w < m->sp_track_w);
        const int32_t cap_y = m->sp_icon_row_y + m->sp_icon + m->sp_caption_gap;
        ASSERT_TRUE(cap_y + m->sp_caption_h <= m->h - m->sp_ver_inset - lh(m->font_body)); // caption above the version line
        ASSERT_TRUE(m->sp_hold_h <= m->sp_ver_inset);
        ASSERT_TRUE(m->sp_ring <= m->sp_icon && m->sp_hap_h <= m->sp_icon);
        ASSERT_TRUE(lh(m->font_large) <= m->sp_icon);
        ASSERT_TRUE(m->sp_diag_a[1].x < m->sp_icon && m->sp_diag_a[1].y < m->sp_icon && m->sp_diag_b[0].x < m->sp_icon);
        ASSERT_TRUE(m->sp_wordmark_y > 0);
    }
}

// Every boot-splash caption (the longest "[n/m] name: " prefix included) must fit ONE line of the caption label in every profile.
TEST(splash_captions_fit_one_line_in_every_profile) {
    static const char *const briefs[] = {BOOT_BRIEF_ALL};
    lv_init();
    lv_text_attributes_t attr = {0};
    size_t n;
    const ui_metrics_t *const *p = ui_metrics_profiles(&n);
    for (size_t k = 0; k < n; k++) {
        const int32_t avail = p[k]->w - 2 * p[k]->pad;
        for (size_t i = 0; i < sizeof briefs / sizeof briefs[0]; i++) {
            char line[96];
            snprintf(line, sizeof line, "[6/6] bluetooth: %s", briefs[i]);
            int32_t w = lv_text_get_width(line, (uint32_t)strlen(line), p[k]->font_body, &attr);
            if (w > avail) {
                printf("  %s: caption too wide: %d px > %d: '%s'\n", p[k]->name, (int)w, (int)avail, line);
            }
            ASSERT_TRUE(w <= avail);
        }
    }
}

TEST(selection_picks_the_largest_profile_that_fits) {
    ASSERT_TRUE(strcmp(ui_metrics_select(320, 240)->name, "320x240") == 0);
    ASSERT_TRUE(strcmp(ui_metrics_select(200, 100)->name, "320x240") == 0); // too small for any: the base profile
    ASSERT_TRUE(strcmp(ui_metrics_select(479, 360)->name, "320x240") == 0);
    ASSERT_TRUE(strcmp(ui_metrics_select(480, 359)->name, "320x240") == 0);
    // the default before any ui_metrics_set() is the product panel's
    ASSERT_TRUE(strcmp(ui_metrics()->name, "320x240") == 0);
#ifdef NEUPOD_UI_HIRES // hosted builds only: the product build has the one profile
    ASSERT_TRUE(strcmp(ui_metrics_select(480, 360)->name, "480x360") == 0);
    ASSERT_TRUE(strcmp(ui_metrics_select(640, 480)->name, "480x360") == 0);
    ui_metrics_set(ui_metrics_select(480, 360));
    ASSERT_EQ(480, ui_metrics()->w);
    ui_metrics_set(ui_metrics_select(320, 240));
#else
    ASSERT_TRUE(strcmp(ui_metrics_select(480, 360)->name, "320x240") == 0);
#endif
}

// ui_text_has_glyph() (the display-text filter) claims exactly the glyphs the compiled fonts have: a drift either draws empty boxes (claimed
// but missing) or turns a drawable character into '?' (present but not claimed).
static bool font_has(const lv_font_t *f, uint32_t cp) {
    lv_font_glyph_dsc_t d;
    return lv_font_get_glyph_dsc(f, &d, cp, 0) && !d.is_placeholder;
}

TEST(text_filter_matches_the_fonts) {
    const lv_font_t *fonts[] = {&neupod_font_12, &neupod_font_14, &neupod_font_20,
#ifdef NEUPOD_UI_HIRES
                                &neupod_font_16, &neupod_font_28,
#endif
    };
    for (size_t i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        uint32_t claimed = 0;
        for (uint32_t cp = 0x20; cp < 0x30000u; cp++) {
            bool want = ui_text_has_glyph(cp);
            claimed += want ? 1u : 0u;
            if (want != font_has(fonts[i], cp)) {
                printf("  font %zu: U+%04X claimed=%d\n", i, (unsigned)cp, want);
            }
            ASSERT_EQ(want, font_has(fonts[i], cp));
        }
        ASSERT_TRUE(claimed > 190);
    }
    // The 48 pt font is for the wordmark and the note symbol only: ASCII and LV_SYMBOL_AUDIO.
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        ASSERT_TRUE(font_has(&neupod_font_48, cp));
    }
    ASSERT_TRUE(font_has(&neupod_font_48, 0xF001));
}

// The two themes (app/ui/ui_theme.c, DS-1): every token is RGB565-safe (red and blue multiples of 8, green of 4: what the panel shows is what was
// written), the text tokens are readable on the surfaces they are drawn on (WCAG contrast), and selecting a theme changes what a token resolves to.
static double lum(uint32_t rgb) {
    double c[3] = {((rgb >> 16) & 255u) / 255.0, ((rgb >> 8) & 255u) / 255.0, (rgb & 255u) / 255.0};
    for (int i = 0; i < 3; i++) {
        c[i] = c[i] <= 0.03928 ? c[i] / 12.92 : pow((c[i] + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

static double contrast(uint32_t a, uint32_t b) {
    double la = lum(a), lb = lum(b);
    return (la > lb ? la + 0.05 : lb + 0.05) / (la > lb ? lb + 0.05 : la + 0.05);
}

TEST(themes_are_rgb565_safe_and_readable) {
    const ui_theme_t *themes[2] = {&k_ui_theme_light, &k_ui_theme_dark};
    for (int t = 0; t < 2; t++) {
        const ui_theme_t *th = themes[t];
        for (int k = 0; k < UI_TOK_COUNT; k++) {
            uint32_t c = th->c[k];
            ASSERT_TRUE(c <= 0xFFFFFFu);
            ASSERT_TRUE(((c >> 16) & 7u) == 0 || ((c >> 16) & 255u) == 255u); // red: multiple of 8 (full scale 255 is exact too)
            ASSERT_TRUE(((c >> 8) & 3u) == 0 || ((c >> 8) & 255u) == 255u);   // green: multiple of 4
            ASSERT_TRUE((c & 7u) == 0 || (c & 255u) == 255u);                 // blue: multiple of 8
        }
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK], th->c[UI_TOK_BG]) >= 7.0);
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK], th->c[UI_TOK_BAR]) >= 7.0);
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK2], th->c[UI_TOK_SURFACE]) >= 4.5);
        // white on the accent: 6.4:1 light (the review quotes 7.3), about 5:1 dark (owner decision: dark accent #2868D8)
        ASSERT_TRUE(contrast(th->c[UI_TOK_ON_ACCENT], th->c[UI_TOK_ACCENT]) >= (t == 0 ? 6.0 : 4.5));
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK], th->c[UI_TOK_ACCENT_SOFT]) >= 7.0);
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK], th->c[UI_TOK_AMBER_SOFT]) >= 7.0);
        ASSERT_TRUE(contrast(th->c[UI_TOK_INK3], th->c[UI_TOK_SURFACE]) >= 2.5); // hints and disabled rows only
    }
    // the review's published values
    ASSERT_EQ(0x1858C8u, k_ui_theme_light.c[UI_TOK_ACCENT]);
    ASSERT_EQ(0x2868D8u, k_ui_theme_dark.c[UI_TOK_ACCENT]);
    ASSERT_EQ(0xF8F8F8u, k_ui_theme_light.c[UI_TOK_BG]);
    ASSERT_EQ(0x101010u, k_ui_theme_dark.c[UI_TOK_BG]);
}

TEST(a_token_follows_the_active_theme_and_a_literal_does_not) {
    lv_init();
    ASSERT_TRUE(ui_theme() == &k_ui_theme_light); // Light is the default
    ASSERT_EQ(0x181818u, ui_rgb(UI_C_INK));
    ASSERT_EQ(0x123456u, ui_rgb(0x123456u));
    ui_theme_set(&k_ui_theme_dark);
    ASSERT_EQ(0xF0F0F0u, ui_rgb(UI_C_INK));
    ASSERT_EQ(0x123456u, ui_rgb(0x123456u));
    ui_theme_set(&k_ui_theme_light);
    ASSERT_EQ(0x181818u, ui_rgb(UI_C_INK));
}

int main(void) {
    RUN(profile_320_is_the_design_system_layout);
    RUN(every_profile_fits_its_panel);
    RUN(splash_captions_fit_one_line_in_every_profile);
    RUN(selection_picks_the_largest_profile_that_fits);
    RUN(text_filter_matches_the_fonts);
    RUN(themes_are_rgb565_safe_and_readable);
    RUN(a_token_follows_the_active_theme_and_a_literal_does_not);
    return RESULT();
}
