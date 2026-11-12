// SPDX-License-Identifier: MPL-2.0
// Reads the PPM snapshots written by `neupod --tour-seek` (ctest seek_tour at 320x240, hires_seek_tour at 480x360) and checks that the
// Now Playing seek mode is drawn at both metrics profiles (ADR-0018): the progress bar is highlighted (orange track, orange fill) and the
// volume bar greys out while the wheel seeks, and both go back to the usual blue / grey when it does not.
//   usage: test_seek_snapshot DIR WIDTH
//   seek-volume       volume mode: grey progress track, blue volume bar
//   seek-mode         seek mode: tinted track, grey volume bar
//   seek-scrub        a scrub in progress: the orange fill reaches part-way along the bar
//   seek-timeout      3 s without input: back to volume mode
//   seek-mp3          MP3: Select enters seek mode (seekable since UI-7: a frame found from the byte proportion / Xing table)
//   seek-unavailable  FLAC without a seek table: Select did not enter seek mode
// The geometry is the Now Playing metrics (ui_metrics_320.h / ui_metrics.c, np_*; status bar included), times 3/2 at 480 wide.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unit.h"

typedef struct {
    int w, h;
    uint8_t *px; // RGB
} ppm_t;

static bool load_ppm(const char *dir, const char *name, ppm_t *o) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("  cannot open %s\n", path);
        return false;
    }
    int maxv = 0;
    if (fscanf(f, "P6 %d %d %d", &o->w, &o->h, &maxv) != 3 || maxv != 255) {
        fclose(f);
        return false;
    }
    (void)fgetc(f); // the single whitespace after the header
    o->px = malloc((size_t)o->w * (size_t)o->h * 3u);
    size_t got = fread(o->px, 3, (size_t)o->w * (size_t)o->h, f);
    fclose(f);
    return got == (size_t)o->w * (size_t)o->h;
}

static int dist(const ppm_t *p, int x, int y, uint32_t rgb) {
    const uint8_t *q = p->px + ((size_t)y * (size_t)p->w + (size_t)x) * 3u;
    int d = abs(q[0] - (int)((rgb >> 16) & 255u)), e = abs(q[1] - (int)((rgb >> 8) & 255u)), f = abs(q[2] - (int)(rgb & 255u));
    return d > e ? (d > f ? d : f) : (e > f ? e : f);
}

// design-system tokens, light theme (app/ui/ui_theme.c)
#define COL_TRACK 0xE0E0E0      // line
#define COL_SEEK_TRACK 0xF8E8C8 // amber_soft
#define COL_SEEK 0xD08018       // amber
#define COL_BLUE 0x1858C8       // accent
#define COL_GREY 0xA0A0A0       // ink3
#define TOL 14

static const char *g_dir;
static int g_w;
// x3 / 2 at 480 wide (S() in ui_metrics.c), exact at 320
static int sc(int v) { return g_w == 480 ? v * 3 / 2 : v; }
static int bar_y(void) { return sc(24) + sc(142) + sc(10) / 2; }                    // progress bar, middle row
static int vol_y(void) { return sc(24) + sc(190) + sc(6) / 2; }                     // volume bar, middle row
static int bar_x_start(void) { return sc(14) + 3; }                                 // inside the left end of the bar
static int bar_x_end(void) { return g_w - sc(14) - 3; }                             // inside the right end
static int vol_x_start(void) { return sc(140) + 3; }
static int vol_x_end(void) { return g_w - sc(14) - 3; }

static bool load(const char *name, ppm_t *p) {
    if (!load_ppm(g_dir, name, p) || p->px == NULL) {
        return false;
    }
    ASSERT_EQ(g_w, p->w);
    return p->w == g_w;
}

TEST(volume_mode_is_the_usual_grey_and_blue) {
    ppm_t p;
    ASSERT_TRUE(load("seek-volume", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_BLUE) < TOL);
    free(p.px);
}

TEST(seek_mode_highlights_the_progress_bar_and_greys_the_volume_bar) {
    ppm_t p;
    ASSERT_TRUE(load("seek-mode", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL);  // the whole bar, to its right end
    ASSERT_TRUE(dist(&p, (bar_x_start() + bar_x_end()) / 2, bar_y(), COL_SEEK_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_GREY) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_end(), vol_y(), COL_TRACK) < TOL);       // the unfilled volume track is unchanged
    free(p.px);
}

TEST(a_scrub_fills_the_bar_in_the_seek_colour_part_way) {
    ppm_t p;
    ASSERT_TRUE(load("seek-scrub", &p));
    ASSERT_TRUE(dist(&p, bar_x_start(), bar_y(), COL_SEEK) < TOL);              // 8 s of 20 s: the left part is filled ...
    int third = bar_x_start() + (bar_x_end() - bar_x_start()) / 3;
    ASSERT_TRUE(dist(&p, third, bar_y(), COL_SEEK) < TOL);
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL);          // ... and the right end is not
    free(p.px);
}

TEST(three_seconds_of_rest_return_to_volume_colours) {
    ppm_t p;
    ASSERT_TRUE(load("seek-timeout", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, bar_x_start(), bar_y(), COL_BLUE) < TOL);              // the played part keeps its place, in blue again
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_BLUE) < TOL);
    free(p.px);
}

TEST(select_on_an_mp3_enters_seek_mode) {
    ppm_t p;
    ASSERT_TRUE(load("seek-mp3", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_GREY) < TOL);
    free(p.px);
}

TEST(select_on_a_flac_without_a_seek_table_does_not_enter_seek_mode) {
    ppm_t p;
    ASSERT_TRUE(load("seek-unavailable", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_BLUE) < TOL);
    free(p.px);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s DIR WIDTH\n", argv[0]);
        return 2;
    }
    g_dir = argv[1];
    g_w = atoi(argv[2]);
    RUN(volume_mode_is_the_usual_grey_and_blue);
    RUN(seek_mode_highlights_the_progress_bar_and_greys_the_volume_bar);
    RUN(a_scrub_fills_the_bar_in_the_seek_colour_part_way);
    RUN(three_seconds_of_rest_return_to_volume_colours);
    RUN(select_on_an_mp3_enters_seek_mode);
    RUN(select_on_a_flac_without_a_seek_table_does_not_enter_seek_mode);
    return RESULT();
}
