// SPDX-License-Identifier: MPL-2.0
// Reads the PPM snapshots written by `neupod --tour-seek` (ctest seek_tour at 320x240, hires_seek_tour at 480x360) and checks that Now Playing's seek
// mode (hold Select) is drawn at both metrics profiles (ADR-0018): the progress bar turns amber (with its soft amber track), the volume strip is replaced
// by the hints, Select applies the scrub, Menu drops it, and a track that cannot seek stays as it was.
//   usage: test_seek_snapshot DIR WIDTH
//   seek-volume       paused on the 20 s WAV: grey track, no fill, the volume strip (grey fill)
//   seek-mode         Select held: soft amber track, volume strip gone
//   seek-scrub        8 wheel steps: the amber fill reaches part-way along the bar
//   seek-cancel       Menu: back to the usual grey track, the position unchanged
//   seek-go           Select applied a scrub: usual colours again, the fill moved on (blue)
//   seek-mp3          MP3: the hold enters seek mode too
//   seek-unavailable  FLAC without a seek table: the hold did not enter seek mode
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
#define TOL 14

static const char *g_dir;
static int g_w;
// x3 / 2 at 480 wide (S() in ui_metrics.c), exact at 320
static int sc(int v) { return g_w == 480 ? v * 3 / 2 : v; }
static int bar_y(void) { return sc(24) + sc(142) + sc(4) / 2; }  // progress bar, middle row (the seek bar is taller but shares its centre)
static int vol_y(void) { return sc(24) + sc(196) + sc(4) / 2; }  // volume bar, middle row
static int bar_x_start(void) { return sc(10) + 3; }              // inside the left end of the bar
static int bar_x_end(void) { return g_w - sc(10) - 3; }          // inside the right end
static int vol_x_start(void) { return sc(144) + 3; }
static int vol_x_end(void) { return g_w - sc(10) - 3; }

static bool load(const char *name, ppm_t *p) {
    if (!load_ppm(g_dir, name, p) || p->px == NULL) {
        return false;
    }
    ASSERT_EQ(g_w, p->w);
    return p->w == g_w;
}

#define COL_BG 0xF8F8F8
#define COL_VOL 0x606060 // ink2

TEST(volume_mode_is_the_usual_grey_track_and_volume_strip) {
    ppm_t p;
    ASSERT_TRUE(load("seek-volume", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_VOL) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_end(), vol_y(), COL_TRACK) < TOL);
    free(p.px);
}

TEST(holding_select_turns_the_bar_amber_and_replaces_the_volume_strip) {
    ppm_t p;
    ASSERT_TRUE(load("seek-mode", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL); // the whole bar, to its right end
    ASSERT_TRUE(dist(&p, (bar_x_start() + bar_x_end()) / 2, bar_y(), COL_SEEK_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_end(), vol_y(), COL_BG) < TOL);
    free(p.px);
}

TEST(a_scrub_fills_the_bar_in_the_seek_colour_part_way) {
    ppm_t p;
    ASSERT_TRUE(load("seek-scrub", &p));
    ASSERT_TRUE(dist(&p, bar_x_start(), bar_y(), COL_SEEK) < TOL);              // 8 steps of 1 s on a 20 s track: the left part is filled ...
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL);          // ... and the right end is not
    free(p.px);
}

TEST(menu_cancels_and_the_usual_colours_return) {
    ppm_t p;
    ASSERT_TRUE(load("seek-cancel", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_VOL) < TOL);
    free(p.px);
}

TEST(select_applies_the_scrub_and_the_bar_is_blue_again) {
    ppm_t p;
    ASSERT_TRUE(load("seek-go", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, bar_x_start(), bar_y(), COL_BLUE) < TOL);              // the played part (the scrub's target) is in the accent colour
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_VOL) < TOL);
    free(p.px);
}

TEST(holding_select_on_an_mp3_enters_seek_mode) {
    ppm_t p;
    ASSERT_TRUE(load("seek-mp3", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_SEEK_TRACK) < TOL);
    free(p.px);
}

TEST(holding_select_on_a_flac_without_a_seek_table_does_not_enter_seek_mode) {
    ppm_t p;
    ASSERT_TRUE(load("seek-unavailable", &p));
    ASSERT_TRUE(dist(&p, bar_x_end(), bar_y(), COL_TRACK) < TOL);
    ASSERT_TRUE(dist(&p, vol_x_start(), vol_y(), COL_VOL) < TOL);
    free(p.px);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s DIR WIDTH\n", argv[0]);
        return 2;
    }
    g_dir = argv[1];
    g_w = atoi(argv[2]);
    RUN(volume_mode_is_the_usual_grey_track_and_volume_strip);
    RUN(holding_select_turns_the_bar_amber_and_replaces_the_volume_strip);
    RUN(a_scrub_fills_the_bar_in_the_seek_colour_part_way);
    RUN(menu_cancels_and_the_usual_colours_return);
    RUN(select_applies_the_scrub_and_the_bar_is_blue_again);
    RUN(holding_select_on_an_mp3_enters_seek_mode);
    RUN(holding_select_on_a_flac_without_a_seek_table_does_not_enter_seek_mode);
    return RESULT();
}
