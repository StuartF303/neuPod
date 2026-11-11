// SPDX-License-Identifier: MPL-2.0
// Music library index at scale (ADR-0017, L1): a synthetic tree of 100,000 audio files on a FatFs disk image, built with the 32 KB
// scratch the ADR allows, then browsed. Slow (about a minute): registered in ctest as "library_big" with the label "slow".
// Prints the numbers the design cares about: build time, scratch peak, sort work, reader memory, index size, page-cache traffic.
#include <time.h>

#include "library_fixture.h"

#include "../app/library/lib_internal.h" /* lib_files: the names of the index files */
#include "library.h"
#include "meta.h"
#include "unit.h"

#define TRACKS 100000u
#define PER_ALBUM 10u
#define ALBUMS_PER_ARTIST 20u

static uint8_t g_scratch[LIB_SCRATCH_MIN] __attribute__((aligned(8)));
static lib_reader_t g_rd;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint32_t lcg(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

TEST(hundred_thousand_tracks) {
    double t0 = now_s();
    ASSERT_TRUE(fx_volume(320ull << 20, 512));
    static uint8_t file[1024];
    uint32_t seed = 12345;
    static const char *const genres[] = {"Rock", "Pop", "Jazz", "Classical", "Electronic", "Folk", "Metal", "Hip-Hop", "(17)", "(13)"};
    for (uint32_t i = 0; i < TRACKS; i++) {
        uint32_t album = i / PER_ALBUM, artist = album / ALBUMS_PER_ARTIST, n = i % PER_ALBUM;
        char path[128], title[48], art[32], alb[32];
        snprintf(path, sizeof path, "/Music/Artist %04u/Album %02u/%02u.flac", artist, album % ALBUMS_PER_ARTIST, n);
        snprintf(title, sizeof title, "Song %c%c %05u", 'A' + (int)(lcg(&seed) % 26), 'a' + (int)(lcg(&seed) % 26), lcg(&seed) % 100000);
        snprintf(art, sizeof art, "Artist %04u", artist);
        snprintf(alb, sizeof alb, "Album %02u", album % ALBUMS_PER_ARTIST);
        fx_tags_t t = {.title = title, .artist = art, .album = alb, .genre = genres[artist % 10], .track = (int)n + 1, .disc = 1, .year = 1970 + (int)(artist % 50)};
        size_t len = fx_flac(file, &t, 200);
        ASSERT_TRUE(fx_put(path, file, len));
        if (unit_test_failed) return;
    }
    double t_make = now_s() - t0;
    int failed0 = unit_test_failed;

    lib_build_stats_t st;
    t0 = now_s();
    lib_err_t e = lib_build(&(lib_build_cfg_t){0}, g_scratch, sizeof g_scratch, &st);
    double t_build = now_s() - t0;
    ASSERT_EQ(LIB_OK, e);
    ASSERT_EQ(TRACKS, st.tracks);
    ASSERT_EQ(TRACKS, st.playable);
    ASSERT_EQ(TRACKS / PER_ALBUM / ALBUMS_PER_ARTIST, st.artists);
    ASSERT_EQ(TRACKS / PER_ALBUM, st.albums);
    ASSERT_EQ(8, st.genres); /* "(17)" and "(13)" are Rock and Pop */
    ASSERT_TRUE(st.scratch_peak <= LIB_SCRATCH_MIN);

    uint64_t bytes = 0;
    for (unsigned i = 0; i < LIB_FILES; i++) {
        char p[64];
        snprintf(p, sizeof p, "/.neupod/lib/%s", lib_files[i].name);
        FILINFO fi;
        if (f_stat(p, &fi) == FR_OK) bytes += fi.fsize;
    }

    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    ASSERT_EQ(TRACKS, lib_list_count(&g_rd, LIB_LIST_SONGS));

    /* every list is in order end to end, every id appears once */
    t0 = now_s();
    static uint8_t seen[TRACKS / 8 + 1];
    char prev[LIB_TEXT_MAX] = "";
    for (uint32_t i = 0; i < TRACKS; i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &r));
        if (lib_collate_cmp(prev, NULL, r.text, NULL) > 0) {
            ASSERT_TRUE(false);
            break;
        }
        ASSERT_TRUE(r.id < TRACKS && !(seen[r.id / 8] & (1u << (r.id % 8))));
        seen[r.id / 8] |= (uint8_t)(1u << (r.id % 8));
        snprintf(prev, sizeof prev, "%s", r.text);
        if (unit_test_failed != failed0) break;
    }
    double t_scan = now_s() - t0;
    char pa[LIB_TEXT_MAX] = "";
    for (uint32_t i = 0; i < lib_list_count(&g_rd, LIB_LIST_ARTISTS); i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_ARTISTS, i, &r));
        ASSERT_TRUE(strcmp(pa, r.text) < 0); /* "Artist 0000".. sorted, each once */
        ASSERT_EQ(ALBUMS_PER_ARTIST, r.count);
        snprintf(pa, sizeof pa, "%s", r.text);
    }

    /* artist -> albums -> tracks ranges are consistent with the files on the card, for a sample */
    uint32_t sample_seed = 99;
    uint32_t hits0 = g_rd.page_hits, miss0 = g_rd.page_misses;
    t0 = now_s();
    for (unsigned k = 0; k < 1000; k++) {
        uint32_t a = lcg(&sample_seed) % st.artists;
        lib_entity_t ar, al;
        ASSERT_EQ(LIB_OK, lib_artist(&g_rd, a, &ar));
        ASSERT_EQ(ALBUMS_PER_ARTIST, ar.count);
        uint32_t b = ar.first + lcg(&sample_seed) % ar.count;
        ASSERT_EQ(LIB_OK, lib_album(&g_rd, b, &al));
        ASSERT_EQ(PER_ALBUM, al.count);
        ASSERT_EQ(a, al.artist);
        uint32_t tid;
        lib_track_t t;
        char p[160];
        FILINFO fi;
        ASSERT_EQ(LIB_OK, lib_by_album_at(&g_rd, al.first + lcg(&sample_seed) % al.count, &tid));
        ASSERT_EQ(LIB_OK, lib_track(&g_rd, tid, &t));
        ASSERT_EQ(b, t.album);
        ASSERT_EQ(a, t.artist);
        ASSERT_EQ(LIB_OK, lib_track_path(&g_rd, tid, p, sizeof p));
        ASSERT_EQ(FR_OK, f_stat(p, &fi));
    }
    double t_rand = now_s() - t0;
    uint32_t rand_hits = g_rd.page_hits - hits0, rand_miss = g_rd.page_misses - miss0;

    /* a screenful: 10 rows at a scroll position, cold then warm */
    lib_close(&g_rd);
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    uint32_t cold0 = g_rd.page_misses;
    for (uint32_t i = 50000; i < 50010; i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &r));
    }
    uint32_t cold = g_rd.page_misses - cold0, warm0 = g_rd.page_misses;
    for (uint32_t i = 50000; i < 50010; i++) {
        lib_row_t r;
        ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_SONGS, i, &r));
    }
    uint32_t warm = g_rd.page_misses - warm0;
    /* Songs rows are random access into tracks.bin and strings.str (title order is not track order), so the four pages cannot hold a
     * screen of them: the UI has to keep its own rendered rows. Artists and Albums are contiguous: a screen is a page or two and
     * redrawing it costs nothing. */
    ASSERT_TRUE(cold <= 10 * 4);
    for (unsigned pass = 0; pass < 2; pass++) {
        uint32_t m0 = g_rd.page_misses;
        for (uint32_t i = 200; i < 210; i++) {
            lib_row_t r;
            ASSERT_EQ(LIB_OK, lib_list_row(&g_rd, LIB_LIST_ARTISTS, i, &r));
        }
        if (pass == 1) ASSERT_EQ(0, g_rd.page_misses - m0); /* the artists' screen, drawn again, is all cache hits */
    }

    /* the whole set verifies */
    lib_close(&g_rd);
    lib_manifest_t mm;
    t0 = now_s();
    ASSERT_EQ(LIB_OK, lib_manifest_load("/.neupod/lib", true, &mm));
    double t_verify = now_s() - t0;

    printf("  files created        : %u in %.1f s\n", TRACKS, t_make);
    printf("  build                : %.1f s (%u dirs, %u artists, %u albums, %u genres)\n", t_build, st.dirs, st.artists, st.albums, st.genres);
    printf("  scratch              : peak %u of %u bytes\n", st.scratch_peak, (unsigned)sizeof g_scratch);
    printf("  external sort        : %u runs written, %u merge steps\n", st.sort_runs, st.sort_merges);
    printf("  index on the card    : %.2f MB\n", (double)bytes / 1048576.0);
    printf("  reader               : %u bytes in the caller's struct (FIL x11 = %u, page cache %u), no heap\n", (unsigned)sizeof(lib_reader_t),
           (unsigned)(11 * sizeof(FIL)), LIB_PAGES * LIB_PAGE_BYTES);
    printf("  ordered scan 100k    : %.1f s; 1000 random drill-downs %.2f s (%u page hits, %u misses)\n", t_scan, t_rand, rand_hits, rand_miss);
    printf("  one 10-row Songs screen: %u page reads cold, %u when drawn again\n", cold, warm);
    printf("  full CRC verify      : %.1f s\n", t_verify);
}

int main(void) {
    RUN(hundred_thousand_tracks);
    fx_cleanup();
    return RESULT();
}
