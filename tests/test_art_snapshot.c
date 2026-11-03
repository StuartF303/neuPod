// SPDX-License-Identifier: MPL-2.0
// Reads the PPM snapshots written by `neupod --tour-art` (ctest art_tour) and checks the Now Playing art box:
//   art-cover     600x600 Cover.JPG beside the track  -> the four colour quadrants, filling the 112 px box
//   art-embedded  500x333 JPEG inside the FLAC        -> the quadrants, letterboxed (dark bars above and below)
//   art-none      PNG inside + progressive cover.jpg  -> the note placeholder, no picture colours
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

// The art box is 112 x 112 at (14, 38) on the 320 x 240 screen (status bar 24 px, box 14 px below it).
#define BX 14
#define BY 38

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
    // the note glyph is light on the dark background: some pixel in the middle of the box is bright
    int bright = 0;
    for (int y = BY + 30; y < BY + 82; y++) {
        for (int x = BX + 30; x < BX + 82; x++) {
            const uint8_t *q = p.px + ((size_t)y * (size_t)p.w + (size_t)x) * 3u;
            if (q[0] > 120 && q[1] > 130 && q[2] > 150) bright++;
        }
    }
    ASSERT_TRUE(bright > 100);
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
    RESULT();
}
