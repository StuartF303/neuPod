// SPDX-License-Identifier: MPL-2.0
// Layout invariants of every UI metrics profile (app/ui/ui_metrics.c): the numbers are static, so "does the layout fit the panel"
// can be checked without drawing. The 320x240 profile is also pinned to the values the UI had before profiles existed (the host
// snapshots at that size are compared byte for byte, so those numbers may not drift). Runs on every profile compiled in.
#include <stdlib.h>
#include <string.h>

#include "boot_briefs.h"
#include "lvgl.h"
#include "fonts/neupod_fonts.h"
#include "src/misc/lv_text_private.h" /* lv_text_get_width: LVGL private API, fine for a host test */
#include "ui_logic.h"
#include "ui_metrics.h"
#include "unit.h"

#define CELLS 5 /* ui_input.c: letters shown in the text picker */
#define CAPS 8  /* capabilities.h CAP_COUNT: subsystem icons on the splash */

void neupod_panic_assert(const char *file, int line) {
    (void)file;
    (void)line;
    abort(); // LVGL's assert hook; not expected to fire
}

static int32_t lh(const lv_font_t *f) { return lv_font_get_line_height(f); }

TEST(profile_320_is_the_historic_layout) {
    const ui_metrics_t *m = ui_metrics_select(320, 240);
    ASSERT_TRUE(strcmp(m->name, "320x240") == 0);
    ASSERT_EQ(320, m->w);
    ASSERT_EQ(240, m->h);
    ASSERT_EQ(24, m->status_h);
    ASSERT_EQ(24, m->row_h);
    ASSERT_EQ(9, m->rows);
    ASSERT_EQ(216, m->content_h);
    ASSERT_EQ(24, m->buf_lines);
    ASSERT_TRUE(m->font_body == &neupod_font_14);
    ASSERT_TRUE(m->font_large == &neupod_font_20);
    ASSERT_TRUE(m->font_huge == &neupod_font_48);
    ASSERT_EQ(112, m->np_art);
    ASSERT_EQ(96, m->home_art);
    ASSERT_EQ(160, m->home_list_w);
    ASSERT_EQ(170, m->set_list_w);
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
        ASSERT_TRUE(m->np_margin + m->np_art <= m->np_bar_y); // art box fits above the progress bar
        ASSERT_TRUE(m->np_text_x < m->w - m->np_margin);
        ASSERT_TRUE(m->np_title_y + lh(m->font_large) <= m->np_artist_y);
        ASSERT_TRUE(m->np_artist_y + lh(m->font_body) <= m->np_album_y);
        ASSERT_TRUE(m->np_album_y + lh(m->font_body) <= m->np_badge_y);
        ASSERT_TRUE(m->np_badge_y + lh(m->font_body) <= m->np_state_y);
        ASSERT_TRUE(m->np_state_y + 2 * lh(m->font_body) <= m->np_bar_y); // two wrapped status lines
        ASSERT_TRUE(m->np_bar_y + m->np_bar_h <= m->np_time_y);
        ASSERT_TRUE(m->np_time_y + lh(m->font_body) <= m->np_vol_y);
        ASSERT_TRUE(m->np_vol_y + lh(m->font_body) <= m->content_h);
        ASSERT_TRUE(m->np_vol_bar_y + m->np_vol_bar_h <= m->content_h);
        // Test tone: its labels fit under the three list rows
        ASSERT_TRUE(3 * m->row_h + m->tt_y[3] + 2 * lh(m->font_body) <= m->content_h);
        // Settings: list + pane = the screen
        ASSERT_TRUE(m->set_list_w < m->w && m->w - m->set_list_w > 4 * m->set_pane_pad);
        ASSERT_TRUE(m->set_body_y + 4 * lh(m->font_body) <= m->content_h);
        // Input widgets
        ASSERT_TRUE(m->in_cell_w * CELLS <= m->w);
        ASSERT_TRUE(m->in_text_value_y + lh(m->font_large) <= m->in_cell_y);
        ASSERT_TRUE(m->in_cell_y + lh(m->font_large) <= m->in_hint_y);
        ASSERT_TRUE(m->in_hint_y + lh(m->font_body) <= m->content_h);
        ASSERT_TRUE(m->in_num_value_y + lh(m->font_large) <= m->in_bar_y);
        ASSERT_TRUE(m->in_bar_y + m->in_bar_h <= m->in_hint_y);
        ASSERT_TRUE(m->in_bar_x * 2 < m->w);
        ASSERT_TRUE(m->in_dt_x[4] + 2 * lh(m->font_large) <= m->w); // two digits are about one line height wide
        ASSERT_TRUE(m->in_dt_y + lh(m->font_large) <= m->in_hint_y);
        ASSERT_TRUE(m->in_dt_x[3] + m->in_dt_time_shift >= 0); // time-only: the two time fields move left
        ASSERT_TRUE(m->in_overlay <= m->content_h && m->in_overlay <= m->w);
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
    const lv_font_t *fonts[] = {&neupod_font_14, &neupod_font_20,
#ifdef NEUPOD_UI_HIRES
                                &neupod_font_28,
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

int main(void) {
    RUN(profile_320_is_the_historic_layout);
    RUN(every_profile_fits_its_panel);
    RUN(splash_captions_fit_one_line_in_every_profile);
    RUN(selection_picks_the_largest_profile_that_fits);
    RUN(text_filter_matches_the_fonts);
    return RESULT();
}
