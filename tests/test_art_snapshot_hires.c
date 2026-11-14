// SPDX-License-Identifier: MPL-2.0
// The 480x360 twin of test_art_snapshot.c: reads the PPM snapshots written by `neupod --tour-art` run with
// NEUPOD_DISPLAY_SIZE=480x360 (ctest hires_art_tour) and checks the Now Playing art box at the 1.5x metrics profile:
//   art-cover     the four colour quadrants, filling the 168 px box
//   art-embedded  the quadrants, letterboxed (dark bars above and below)
//   art-home      Home: the quadrants in the 144 px box of the right pane, the title text under it, no placeholder ring
//   art-none      the ring placeholder, no picture colours
// The thumbnail is still decoded at 64 px (art.h) and scaled up by LVGL, so the quadrant edges are soft; the probes stay clear of them.
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

static int dist(const ppm_t *p, int x, int y, int r, int g, int b) {
    const uint8_t *q = p->px + ((size_t)y * (size_t)p->w + (size_t)x) * 3u;
    int d = abs(q[0] - r), e = abs(q[1] - g), f = abs(q[2] - b);
    return d > e ? (d > f ? d : f) : (e > f ? e : f);
}

static const char *g_dir;

// The art box is 168 x 168 at (15, 54) on the 480 x 360 screen (status bar 36 px, box 18 px below it).
#define BX 15
#define BY 54
#define BOX 168

TEST(test_cover_quadrants) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-cover", &p));
    if (p.px == NULL) return;
    ASSERT_EQ(480, p.w);
    ASSERT_EQ(360, p.h);
    ASSERT_TRUE(dist(&p, BX + BOX / 4, BY + BOX / 4, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + BOX / 4, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, BX + BOX / 4, BY + 3 * BOX / 4, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + 3 * BOX / 4, 230, 210, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 6, BY + 6, 220, 40, 40) < 48); // a square picture fills the box to its corners
    // nothing of the box leaks past its edge: just outside it is the white screen
    ASSERT_TRUE(dist(&p, BX + BOX + 3, BY + BOX / 2, 255, 255, 255) < 12);
    ASSERT_TRUE(dist(&p, BX + BOX / 2, BY + BOX + 3, 255, 255, 255) < 12);
    free(p.px);
}

TEST(test_embedded_letterboxed) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-embedded", &p));
    if (p.px == NULL) return;
    // 500 x 333 scaled to 168 x 112: rows 28..139 of the box carry the picture, the rest is the dark background
    ASSERT_TRUE(dist(&p, BX + BOX / 4, BY + 28 + 28, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + 28 + 28, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, BX + BOX / 4, BY + 28 + 84, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + 28 + 84, 230, 210, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + BOX / 2, BY + 9, 40, 55, 90) < 70);    // bar above: dark blue-grey background
    ASSERT_TRUE(dist(&p, BX + BOX / 2, BY + 159, 30, 42, 68) < 70);  // bar below
    free(p.px);
}

TEST(test_none_shows_placeholder) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-none", &p));
    if (p.px == NULL) return;
    ASSERT_TRUE(dist(&p, BX + BOX / 4, BY + BOX / 4, 220, 40, 40) > 80); // no red quadrant ...
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + BOX / 4, 40, 200, 60) > 80); // ... no green one ...
    ASSERT_TRUE(dist(&p, BX + 3 * BOX / 4, BY + 3 * BOX / 4, 230, 210, 40) > 80); // ... no yellow one
    // the placeholder's centre dot is a light spot on the dark gradient (no music-note glyph any more)
    const uint8_t *q = p.px + ((size_t)(BY + 84) * (size_t)p.w + (size_t)(BX + 84)) * 3u;
    ASSERT_TRUE(q[0] > 110 && q[1] > 110 && q[2] > 120);
    free(p.px);
}

// Home right pane: the list is 240 px wide, the pane starts below the 36 px status bar, the 144 px art box is at (48, 48) in it.
#define HX (240 + 48)
#define HY (36 + 48)
#define HB 144

TEST(test_home_pane_shows_art_and_title) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-home", &p));
    if (p.px == NULL) return;
    // 500 x 333 scaled to 144 x 96: rows 24..119 of the box carry the picture
    ASSERT_TRUE(dist(&p, HX + HB / 4, HY + 24 + 24, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, HX + 3 * HB / 4, HY + 24 + 24, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, HX + HB / 4, HY + 24 + 72, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, HX + 3 * HB / 4, HY + 24 + 72, 230, 210, 40) < 48);
    // no placeholder ring: the idle Home snapshot (home.ppm, same tour) has its ring edge at (box mid-height, x = 72 - 31); that pixel
    // must differ here (the ring is 44 % of the box wide: radius 31)
    ppm_t idle;
    ASSERT_TRUE(load_ppm(g_dir, "home", &idle));
    if (idle.px != NULL) {
        const uint8_t *r = idle.px + ((size_t)(HY + HB / 2) * (size_t)idle.w + (size_t)(HX + HB / 2 - 31 + 1)) * 3u;
        ASSERT_TRUE(dist(&p, HX + HB / 2 - 31 + 1, HY + HB / 2, r[0], r[1], r[2]) > 25);
        free(idle.px);
    }
    // the title line (y = 210 in the pane) has text pixels near UI_L_TEXT (0x1D1D1B); the dim "Nothing playing" (0x5B5A56) would not count
    int dark = 0;
    for (int y = 36 + 210; y < 36 + 240; y++) {
        for (int x = 240; x < 480; x++) {
            const uint8_t *q = p.px + ((size_t)y * (size_t)p.w + (size_t)x) * 3u;
            if (q[0] < 0x40 && q[1] < 0x40 && q[2] < 0x40) dark++;
        }
    }
    ASSERT_TRUE(dark > 80);
    free(p.px);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s SNAPSHOT_DIR\n", argv[0]);
        return 64;
    }
    g_dir = argv[1];
    RUN(test_cover_quadrants);
    RUN(test_embedded_letterboxed);
    RUN(test_none_shows_placeholder);
    RUN(test_home_pane_shows_art_and_title);
    return RESULT();
}
