// SPDX-License-Identifier: MPL-2.0
// The Music lists (library_view.h) and the queue ranges they hand to the player, on a FatFs disk image with a small tagged library:
// every list's rows, counts and titles, the order tables behind "play this album / artist / all songs / genre" (including the new by-genre
// order), and the path of every queue entry (lib_resolve).
#include "library_fixture.h"

#include "library.h"
#include "library_view.h"
#include "meta.h"
#include "nfs.h"
#include "unit.h"

static uint8_t g_file[16384];
static uint8_t g_scratch[LIB_SCRATCH_MIN] __attribute__((aligned(8)));
static lib_reader_t g_rd;

typedef enum { F_FLAC, F_MP3, F_WAV, F_AAC } kind_t;

static bool put(const char *path, kind_t k, const fx_tags_t *t) {
    size_t n = 0;
    switch (k) {
    case F_FLAC: n = fx_flac(g_file, t, 200); break;
    case F_MP3: n = fx_mp3(g_file, t, false); break;
    case F_WAV: n = fx_wav(g_file, 100); break;
    case F_AAC: n = fx_m4a_aac(g_file, t); break;
    }
    return fx_put(path, g_file, n);
}

/*  Artists (sorted):  Abba (Gold: 2 songs, Arrival: 1)   Beta (Solo: 3 songs, one AAC)   The Cure (Disintegration: 2)   Compilations: Now 1 (2 songs)
 *  Genres:            Pop (Abba x3, Beta Solo track 1)   Rock (Cure x2, Beta Solo track 2)   (Beta track 3 and the compilation songs: Rock / none) */
static bool make_tree(void) {
    bool ok = true;
#define T(...) &(fx_tags_t){__VA_ARGS__}
    ok &= put("/Music/Abba/Gold/02.mp3", F_MP3, T(.title = "Mamma Mia", .artist = "Abba", .album = "Gold", .genre = "Pop", .track = 2));
    ok &= put("/Music/Abba/Gold/01.mp3", F_MP3, T(.title = "Dancing Queen", .artist = "Abba", .album = "Gold", .genre = "Pop", .track = 1));
    ok &= put("/Music/Abba/Arrival/01.mp3", F_MP3, T(.title = "Money Money Money", .artist = "Abba", .album = "Arrival", .genre = "Pop", .track = 1));
    ok &= put("/Music/Beta/Solo/01.flac", F_FLAC, T(.title = "Bravo", .artist = "Beta", .album = "Solo", .genre = "Pop", .track = 1));
    ok &= put("/Music/Beta/Solo/02.flac", F_FLAC, T(.title = "Alpha Song", .artist = "Beta", .album = "Solo", .genre = "Rock", .track = 2));
    ok &= put("/Music/Beta/Solo/03.m4a", F_AAC, T(.title = "Charlie", .artist = "Beta", .album = "Solo", .genre = "Rock", .track = 3));
    ok &= put("/Music/Cure/Dis/01.flac", F_FLAC, T(.title = "Plainsong", .artist = "The Cure", .album = "Disintegration", .genre = "Rock", .track = 1));
    ok &= put("/Music/Cure/Dis/02.flac", F_FLAC, T(.title = "Pictures of You", .artist = "The Cure", .album = "Disintegration", .genre = "Rock", .track = 2));
    ok &= put("/Music/Now/01.flac", F_FLAC, T(.title = "Hit A", .artist = "Q", .album = "Now 1", .track = 1, .comp = true));
    ok &= put("/Music/Now/02.flac", F_FLAC, T(.title = "Hit B", .artist = "Zed", .album = "Now 1", .track = 2, .comp = true));
#undef T
    return ok;
}

static lib_err_t build(void) {
    lib_build_cfg_t cfg = {0};
    return lib_build(&cfg, g_scratch, sizeof g_scratch, NULL);
}

static bool open_lib(void) {
    return fx_volume(16u << 20, 0) && make_tree() && build() == LIB_OK && lib_open(&g_rd, NULL) == LIB_OK;
}

static uint32_t find_in(lvw_view_t v, const char *text) {
    lvw_info_t in;
    if (lvw_info(&g_rd, v, &in) != LIB_OK) return LIB_NONE;
    for (uint32_t i = 0; i < in.count; i++) {
        lvw_row_t r;
        if (lvw_row(&g_rd, v, &in, i, &r) == LIB_OK && strcmp(r.text, text) == 0) return i;
    }
    return LIB_NONE;
}

/* the text of the first @p n rows */
static void rows(lvw_view_t v, uint32_t n, const char *const *want) {
    lvw_info_t in;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, v, &in));
    ASSERT_EQ(n, in.count);
    for (uint32_t i = 0; i < n; i++) {
        lvw_row_t r;
        ASSERT_EQ(LIB_OK, lvw_row(&g_rd, v, &in, i, &r));
        if (strcmp(r.text, want[i]) != 0) printf("  row %u of kind %u: '%s' expected '%s'\n", (unsigned)i, (unsigned)v.kind, r.text, want[i]);
        ASSERT_EQ(0, strcmp(r.text, want[i]));
    }
    lvw_row_t r;
    ASSERT_EQ(LIB_E_RANGE, lvw_row(&g_rd, v, &in, n, &r));
}

/* the paths a range queues, in queue order, against what the list shows */
static void range_paths(const pq_range_t *rg, uint32_t n, const char *const *want) {
    ASSERT_EQ(n, rg->count);
    for (uint32_t i = 0; i < n; i++) {
        char p[128];
        uint8_t play = 99;
        ASSERT_EQ(LIB_OK, lib_resolve(NULL, (lib_order_t)rg->order, rg->first + i, p, sizeof p, &play));
        if (strcmp(p, want[i]) != 0) printf("  range entry %u: '%s' expected '%s'\n", (unsigned)i, p, want[i]);
        ASSERT_EQ(0, strcmp(p, want[i]));
    }
}

TEST(top_level_lists) {
    ASSERT_TRUE(open_lib());
    static const char *const artists[] = {"Abba", "Beta", "The Cure"};
    rows((lvw_view_t){LVW_ARTISTS, 0}, 3, artists);
    static const char *const albums[] = {"Arrival", "Disintegration", "Gold", "Solo"}; /* by name; the compilation is not an artist's album ... */
    lvw_info_t in;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_ALBUMS, 0}, &in));
    ASSERT_EQ(5, in.count); /* ... but it is in the Albums list (4 + Now 1) */
    lvw_row_t r;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_ALBUMS, 0}, &in, 0, &r));
    ASSERT_EQ(0, strcmp(r.text, albums[0]));
    ASSERT_EQ(0, strcmp(r.detail, "Abba")); /* the album artist as the second text */
    ASSERT_EQ(LVW_ALBUM, r.child.kind);
    static const char *const genres[] = {"Pop", "Rock"};
    rows((lvw_view_t){LVW_GENRES, 0}, 2, genres);
    static const char *const comps[] = {"Now 1"};
    rows((lvw_view_t){LVW_COMPILATIONS, 0}, 1, comps);
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_SONGS, 0}, &in));
    ASSERT_EQ(10, in.count);
    ASSERT_TRUE(in.songs);
    ASSERT_EQ(LIB_ORDER_TITLE, in.range.order);
    ASSERT_EQ(0, in.range.first);
    ASSERT_EQ(10, in.range.count);
    ASSERT_EQ(0, strcmp(in.title, "Songs"));
    lib_close(&g_rd);
}

TEST(songs_list_is_collated_with_artist_and_greyed_aac) {
    ASSERT_TRUE(open_lib());
    lvw_view_t v = {LVW_SONGS, 0};
    lvw_info_t in;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, v, &in));
    static const char *const titles[] = {"Alpha Song", "Bravo", "Charlie", "Dancing Queen", "Hit A", "Hit B", "Mamma Mia", "Money Money Money", "Pictures of You", "Plainsong"};
    uint32_t seen_unplayable = 0;
    for (uint32_t i = 0; i < in.count; i++) {
        lvw_row_t r;
        ASSERT_EQ(LIB_OK, lvw_row(&g_rd, v, &in, i, &r));
        ASSERT_EQ(LVW_KINDS, r.child.kind);
        ASSERT_EQ(0, strcmp(r.text, titles[i]));
        if (r.play != LIB_PLAY_OK) {
            seen_unplayable++;
            ASSERT_EQ(0, strcmp(r.text, "Charlie"));
            ASSERT_EQ(LIB_PLAY_CODEC, r.play);
            ASSERT_EQ(0, strcmp(r.detail, "Beta"));
        }
    }
    ASSERT_EQ(1, seen_unplayable);
    lvw_row_t r;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, v, &in, 0, &r));
    ASSERT_EQ(0, strcmp(r.text, "Alpha Song"));
    ASSERT_EQ(0, strcmp(r.detail, "Beta"));
    lib_close(&g_rd);
}

TEST(artist_album_and_songs_in_track_order) {
    ASSERT_TRUE(open_lib());
    uint32_t abba = find_in((lvw_view_t){LVW_ARTISTS, 0}, "Abba");
    ASSERT_TRUE(abba != LIB_NONE);
    lvw_row_t ar;
    lvw_info_t in;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_ARTISTS, 0}, &in));
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_ARTISTS, 0}, &in, abba, &ar));
    ASSERT_EQ(LVW_ARTIST, ar.child.kind);
    lvw_view_t av = ar.child;

    /* the artist: "All Songs" first, then its albums by name (Arrival, Gold), each with its song count */
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, av, &in));
    ASSERT_EQ(3, in.count);
    ASSERT_EQ(1, in.head);
    ASSERT_FALSE(in.songs);
    ASSERT_EQ(0, strcmp(in.title, "Abba"));
    lvw_row_t r0, r1, r2;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, av, &in, 0, &r0));
    ASSERT_EQ(0, strcmp(r0.text, "All Songs"));
    ASSERT_EQ(LVW_ARTIST_SONGS, r0.child.kind);
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, av, &in, 1, &r1));
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, av, &in, 2, &r2));
    ASSERT_EQ(0, strcmp(r1.text, "Arrival"));
    ASSERT_EQ(0, strcmp(r1.detail, "1"));
    ASSERT_EQ(0, strcmp(r2.text, "Gold"));
    ASSERT_EQ(0, strcmp(r2.detail, "2"));
    ASSERT_EQ(LVW_ALBUM, r2.child.kind);

    /* Gold: tracks by track number, not by file name */
    lvw_info_t gold;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, r2.child, &gold));
    ASSERT_TRUE(gold.songs);
    ASSERT_EQ(2, gold.count);
    ASSERT_EQ(0, gold.head);
    ASSERT_EQ(0, strcmp(gold.title, "Gold"));
    ASSERT_EQ(LIB_ORDER_ALBUM, gold.range.order);
    static const char *const gold_paths[] = {"/Music/Abba/Gold/01.mp3", "/Music/Abba/Gold/02.mp3"};
    range_paths(&gold.range, 2, gold_paths);
    static const char *const gold_titles[] = {"Dancing Queen", "Mamma Mia"};
    rows(r2.child, 2, gold_titles);

    /* All Songs of the artist: Arrival's one song, then Gold's two - one range in the by-album order */
    lvw_info_t all;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, r0.child, &all));
    ASSERT_TRUE(all.songs);
    static const char *const abba_paths[] = {"/Music/Abba/Arrival/01.mp3", "/Music/Abba/Gold/01.mp3", "/Music/Abba/Gold/02.mp3"};
    range_paths(&all.range, 3, abba_paths);
    ASSERT_EQ(3, all.count);
    lvw_row_t sr;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, r0.child, &all, 1, &sr));
    ASSERT_EQ(0, strcmp(sr.text, "Dancing Queen"));
    ASSERT_EQ(0, strcmp(sr.detail, "Gold")); /* in a list of an artist's songs the second text is the album */

    /* the artist with the unplayable track: greyed, in its place in the album */
    uint32_t beta = find_in((lvw_view_t){LVW_ARTISTS, 0}, "Beta");
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_ARTISTS, 0}, &(lvw_info_t){.count = 3}, beta, &ar));
    lvw_info_t bi, solo;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, ar.child, &bi));
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, ar.child, &bi, 1, &r1));
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, r1.child, &solo));
    ASSERT_EQ(3, solo.count);
    lvw_row_t third;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, r1.child, &solo, 2, &third));
    ASSERT_EQ(0, strcmp(third.text, "Charlie"));
    ASSERT_EQ(LIB_PLAY_CODEC, third.play);
    char p[128];
    uint8_t play = 0;
    ASSERT_EQ(LIB_OK, lib_resolve(NULL, LIB_ORDER_ALBUM, solo.range.first + 2, p, sizeof p, &play));
    ASSERT_EQ(LIB_PLAY_CODEC, play); /* the queue's resolver says so too, so Next skips it */
    lib_close(&g_rd);
}

TEST(genre_lists_and_the_genre_queue) {
    ASSERT_TRUE(open_lib());
    lvw_info_t in;
    lvw_row_t r;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_GENRES, 0}, &in));
    uint32_t rock = find_in((lvw_view_t){LVW_GENRES, 0}, "Rock");
    ASSERT_TRUE(rock != LIB_NONE);
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_GENRES, 0}, &in, rock, &r));
    lvw_view_t gv = r.child;
    ASSERT_EQ(LVW_GENRE, gv.kind);
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, gv, &in));
    ASSERT_EQ(1, in.head);
    ASSERT_EQ(3, in.count); /* All Songs + Beta + The Cure */
    ASSERT_EQ(0, strcmp(in.title, "Rock"));
    lvw_row_t head, a1, a2;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, gv, &in, 0, &head));
    ASSERT_EQ(LVW_GENRE_SONGS, head.child.kind);
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, gv, &in, 1, &a1));
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, gv, &in, 2, &a2));
    ASSERT_EQ(0, strcmp(a1.text, "Beta"));
    ASSERT_EQ(0, strcmp(a2.text, "The Cure"));
    ASSERT_EQ(LVW_ARTIST, a1.child.kind);

    /* the genre's songs: the by-genre order, album order inside it (Beta's Solo, then Disintegration) */
    lvw_info_t gs;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, head.child, &gs));
    ASSERT_TRUE(gs.songs);
    ASSERT_EQ(LIB_ORDER_GENRE, gs.range.order);
    static const char *const rock_paths[] = {"/Music/Beta/Solo/02.flac", "/Music/Beta/Solo/03.m4a", "/Music/Cure/Dis/01.flac", "/Music/Cure/Dis/02.flac"};
    range_paths(&gs.range, 4, rock_paths);
    ASSERT_EQ(4, gs.count);
    ASSERT_EQ(0, strcmp(gs.title, "Rock"));
    lvw_row_t s0;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, head.child, &gs, 0, &s0));
    ASSERT_EQ(0, strcmp(s0.text, "Alpha Song"));
    ASSERT_EQ(0, strcmp(s0.detail, "Beta")); /* artist as the second text */

    uint32_t pop = find_in((lvw_view_t){LVW_GENRES, 0}, "Pop");
    lvw_info_t gl;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_GENRES, 0}, &gl));
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_GENRES, 0}, &gl, pop, &r));
    lvw_info_t pi;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_GENRE_SONGS, r.child.id}, &pi));
    static const char *const pop_paths[] = {"/Music/Abba/Arrival/01.mp3", "/Music/Abba/Gold/01.mp3", "/Music/Abba/Gold/02.mp3", "/Music/Beta/Solo/01.flac"};
    range_paths(&pi.range, 4, pop_paths);
    ASSERT_EQ(8, lib_manifest(&g_rd)->genre_tracks);
    lib_close(&g_rd);
}

TEST(compilation_album_and_the_whole_library_order) {
    ASSERT_TRUE(open_lib());
    lvw_info_t in;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){LVW_COMPILATIONS, 0}, &in));
    lvw_row_t r;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, (lvw_view_t){LVW_COMPILATIONS, 0}, &in, 0, &r));
    ASSERT_EQ(0, strcmp(r.text, "Now 1"));
    ASSERT_EQ(0, r.detail[0]); /* no album artist */
    lvw_info_t al;
    ASSERT_EQ(LIB_OK, lvw_info(&g_rd, r.child, &al));
    ASSERT_EQ(2, al.count);
    lvw_row_t s;
    ASSERT_EQ(LIB_OK, lvw_row(&g_rd, r.child, &al, 0, &s));
    ASSERT_EQ(0, strcmp(s.text, "Hit A"));
    ASSERT_EQ(0, strcmp(s.detail, "Q")); /* a compilation shows each song's own performer */

    /* every song is reachable by title position exactly once, and each position resolves to a distinct file */
    static char seen[10][96];
    for (uint32_t i = 0; i < 10; i++) {
        uint8_t play;
        ASSERT_EQ(LIB_OK, lib_resolve(NULL, LIB_ORDER_TITLE, i, seen[i], sizeof seen[i], &play));
        for (uint32_t j = 0; j < i; j++) ASSERT_TRUE(strcmp(seen[i], seen[j]) != 0);
    }
    char p[96];
    ASSERT_EQ(LIB_E_RANGE, lib_resolve(NULL, LIB_ORDER_TITLE, 10, p, sizeof p, NULL));
    ASSERT_EQ(LIB_E_RANGE, lib_resolve(NULL, LIB_ORDER_GENRE, 8, p, sizeof p, NULL));
    ASSERT_EQ(LIB_E_RANGE, lib_resolve(NULL, LIB_ORDER_TITLE, 0, p, 5, NULL)); /* path does not fit */
    ASSERT_EQ(LIB_E_RANGE, lib_resolve(NULL, (lib_order_t)7, 0, p, sizeof p, NULL));
    lib_close(&g_rd);
}

TEST(bad_ids_and_no_library) {
    ASSERT_TRUE(open_lib());
    lvw_info_t in;
    ASSERT_EQ(LIB_E_RANGE, lvw_info(&g_rd, (lvw_view_t){LVW_ARTIST, 99}, &in));
    ASSERT_EQ(LIB_E_RANGE, lvw_info(&g_rd, (lvw_view_t){LVW_ALBUM, 99}, &in));
    ASSERT_EQ(LIB_E_RANGE, lvw_info(&g_rd, (lvw_view_t){LVW_GENRE, 99}, &in));
    ASSERT_EQ(LIB_E_RANGE, lvw_info(&g_rd, (lvw_view_t){LVW_KINDS, 0}, &in));
    lib_close(&g_rd);
    /* an empty library: every list is empty, nothing to play */
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    ASSERT_EQ(LIB_OK, build());
    ASSERT_EQ(LIB_OK, lib_open(&g_rd, NULL));
    for (unsigned k = LVW_ARTISTS; k <= LVW_COMPILATIONS; k++) {
        ASSERT_EQ(LIB_OK, lvw_info(&g_rd, (lvw_view_t){(uint8_t)k, 0}, &in));
        ASSERT_EQ(0, in.count);
    }
    lib_close(&g_rd);
    /* no library at all */
    ASSERT_TRUE(fx_volume(16u << 20, 0));
    char p[64];
    ASSERT_EQ(LIB_E_NONE, lib_resolve(NULL, LIB_ORDER_TITLE, 0, p, sizeof p, NULL));
    ASSERT_EQ(LIB_E_NONE, lib_open(&g_rd, NULL));
}

TEST(utf8_text_is_cut_on_a_character_boundary) {
    char out[8];
    lvw_copy(out, sizeof out, "abcdefghij");
    ASSERT_EQ(0, strcmp(out, "abcdefg"));
    lvw_copy(out, sizeof out, "abcdef\xC3\xA9"); /* e-acute is two bytes: 8 bytes do not fit in 7 */
    ASSERT_EQ(0, strcmp(out, "abcdef"));
    lvw_copy(out, sizeof out, "ab");
    ASSERT_EQ(0, strcmp(out, "ab"));
}

int main(void) {
    RUN(top_level_lists);
    RUN(songs_list_is_collated_with_artist_and_greyed_aac);
    RUN(artist_album_and_songs_in_track_order);
    RUN(genre_lists_and_the_genre_queue);
    RUN(compilation_album_and_the_whole_library_order);
    RUN(bad_ids_and_no_library);
    RUN(utf8_text_is_cut_on_a_character_boundary);
    fx_cleanup();
    return RESULT();
}
