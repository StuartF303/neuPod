// SPDX-License-Identifier: MPL-2.0
// Reads the PPM snapshots written by `neupod --tour-art` (ctest art_tour) and checks the Now Playing art box:
//   art-cover     600x600 Cover.JPG beside the track  -> the four colour quadrants, filling the 112 px box
//   art-embedded  500x333 JPEG inside the FLAC        -> the quadrants, letterboxed (dark bars above and below)
//   art-home      Home while the embedded-art album is paused -> the same quadrants in the 96 px box of the right pane, and the
//                 title text under it (and no placeholder ring)
//   art-none      PNG inside + progressive cover.jpg  -> the ring placeholder, no picture colours
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

// The art box is 112 x 112 at (10, 36) on the 320 x 240 screen (status bar 24 px, box 12 px below it).
#define BX 10
#define BY 36

TEST(test_cover_quadrants) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-cover", &p));
    if (p.px == NULL) return;
    ASSERT_TRUE(dist(&p, BX + 28, BY + 28, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 84, BY + 28, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, BX + 28, BY + 84, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, BX + 84, BY + 84, 230, 210, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 4, BY + 4, 220, 40, 40) < 48); // a square picture fills the box to its corners
    free(p.px);
}

TEST(test_embedded_letterboxed) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-embedded", &p));
    if (p.px == NULL) return;
    // 500 x 333 scaled to 112 x 74: rows 19..92 of the box carry the picture, the rest is the dark background
    ASSERT_TRUE(dist(&p, BX + 28, BY + 19 + 18, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 84, BY + 19 + 18, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, BX + 28, BY + 19 + 55, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, BX + 84, BY + 19 + 55, 230, 210, 40) < 48);
    ASSERT_TRUE(dist(&p, BX + 56, BY + 6, 40, 55, 90) < 70);   // bar above: dark blue-grey background
    ASSERT_TRUE(dist(&p, BX + 56, BY + 106, 30, 42, 68) < 70); // bar below
    free(p.px);
}

TEST(test_none_shows_placeholder) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-none", &p));
    if (p.px == NULL) return;
    ASSERT_TRUE(dist(&p, BX + 28, BY + 28, 220, 40, 40) > 80); // no red quadrant ...
    ASSERT_TRUE(dist(&p, BX + 84, BY + 28, 40, 200, 60) > 80); // ... no green one ...
    ASSERT_TRUE(dist(&p, BX + 84, BY + 84, 230, 210, 40) > 80); // ... no yellow one
    // the placeholder's centre dot is a light spot on the dark gradient (no music-note glyph any more)
    const uint8_t *q = p.px + ((size_t)(BY + 56) * (size_t)p.w + (size_t)(BX + 56)) * 3u;
    ASSERT_TRUE(q[0] > 110 && q[1] > 110 && q[2] > 120);
    free(p.px);
}

// Home right pane: the list is 160 px wide, the pane starts below the 24 px status bar, the 96 px art box is at (32, 32) in it.
#define HX (160 + 32)
#define HY (24 + 32)

TEST(test_home_pane_shows_art_and_title) {
    ppm_t p;
    ASSERT_TRUE(load_ppm(g_dir, "art-home", &p));
    if (p.px == NULL) return;
    // 500 x 333 scaled to 96 x 64: rows 16..79 of the box carry the picture
    ASSERT_TRUE(dist(&p, HX + 24, HY + 16 + 16, 220, 40, 40) < 48);
    ASSERT_TRUE(dist(&p, HX + 72, HY + 16 + 16, 40, 200, 60) < 48);
    ASSERT_TRUE(dist(&p, HX + 24, HY + 16 + 48, 50, 60, 220) < 48);
    ASSERT_TRUE(dist(&p, HX + 72, HY + 16 + 48, 230, 210, 40) < 48);
    // no placeholder ring: the idle Home snapshot (home.ppm, same tour) has its ring edge at (box mid-height, x = 48 - 21); that pixel
    // must differ here, and must really be ring-coloured there (pale against the dark gradient)
    ppm_t idle;
    ASSERT_TRUE(load_ppm(g_dir, "home", &idle));
    if (idle.px != NULL) {
        const uint8_t *r = idle.px + ((size_t)(HY + 48) * (size_t)idle.w + (size_t)(HX + 48 - 21 + 1)) * 3u;
        ASSERT_TRUE(dist(&p, HX + 48 - 21 + 1, HY + 48, r[0], r[1], r[2]) > 25);
        free(idle.px);
    }
    // the title line (y = 140 in the pane) has text pixels near UI_L_TEXT (0x1D1D1B); the dim "Nothing playing" (0x5B5A56) would not count
    int dark = 0;
    for (int y = 24 + 140; y < 24 + 160; y++) {
        for (int x = 160; x < 320; x++) {
            const uint8_t *q = p.px + ((size_t)y * (size_t)p.w + (size_t)x) * 3u;
            if (q[0] < 0x40 && q[1] < 0x40 && q[2] < 0x40) dark++;
        }
    }
    ASSERT_TRUE(dark > 40);
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
