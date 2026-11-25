// SPDX-License-Identifier: MPL-2.0
// Playlists, the pure parts (design review LB-7): the .m3u / .m3u8 reader, path resolution, the playlist text On-The-Go writes, the queue built from a list of
// paths, and the rule that counts a play.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pl_m3u.h"
#include "play_queue.h"
#include "play_tracker.h"
#include "unit.h"

typedef struct {
    pl_entry_t e[16];
    int n;
} got_t;

static void collect(void *user, const pl_entry_t *e) {
    got_t *g = user;
    if (g->n < 16) {
        g->e[g->n++] = *e;
    }
}

static void parse(const char *dir, const char *text, size_t len, got_t *g) {
    pl_m3u_t p;
    g->n = 0;
    pl_m3u_init(&p, dir);
    pl_m3u_feed(&p, (const uint8_t *)text, len, collect, g);
    pl_m3u_end(&p, collect, g);
}

TEST(extinf_gives_title_artist_and_length_and_paths_resolve_against_the_playlists_folder) {
    const char *t = "#EXTM3U\n#EXTINF:215,Aurora Lane - First Light\n../Music/Aurora Lane/First Light/01.flac\n"
                    "#EXTINF:99,Just A Title\n/Music/b.mp3\n\n#PLAYLIST:ignored\nc.wav\n";
    got_t g;
    parse("/Playlists", t, strlen(t), &g);
    ASSERT_EQ(3, g.n);
    ASSERT_TRUE(strcmp(g.e[0].path, "/Music/Aurora Lane/First Light/01.flac") == 0);
    ASSERT_TRUE(strcmp(g.e[0].artist, "Aurora Lane") == 0 && strcmp(g.e[0].title, "First Light") == 0);
    ASSERT_EQ(215, g.e[0].secs);
    ASSERT_TRUE(g.e[0].resolvable);
    ASSERT_TRUE(strcmp(g.e[1].path, "/Music/b.mp3") == 0 && strcmp(g.e[1].title, "Just A Title") == 0 && g.e[1].artist[0] == '\0');
    ASSERT_TRUE(strcmp(g.e[2].path, "/Playlists/c.wav") == 0 && g.e[2].title[0] == '\0'); /* no EXTINF: the caller derives the title */
}

TEST(line_ends_and_a_byte_order_mark_and_a_missing_final_newline) {
    got_t g;
    const char *crlf = "\xEF\xBB\xBF#EXTM3U\r\n/a.flac\r\n/b.flac\r\n/c.flac";
    parse("/", crlf, strlen(crlf), &g);
    ASSERT_EQ(3, g.n);
    ASSERT_TRUE(strcmp(g.e[0].path, "/a.flac") == 0 && strcmp(g.e[2].path, "/c.flac") == 0);
    const char *cr = "/a.flac\r/b.flac\r";
    parse("/", cr, strlen(cr), &g);
    ASSERT_EQ(2, g.n);
    ASSERT_EQ(0, 0);
}

TEST(a_feed_in_small_pieces_gives_the_same_entries) {
    const char *t = "#EXTM3U\n#EXTINF:1,A - B\n/x/y.flac\n/z.mp3\n";
    pl_m3u_t p;
    got_t g = {.n = 0};
    pl_m3u_init(&p, "/");
    for (size_t i = 0; i < strlen(t); i++) {
        pl_m3u_feed(&p, (const uint8_t *)t + i, 1, collect, &g);
    }
    pl_m3u_end(&p, collect, &g);
    ASSERT_EQ(2, g.n);
    ASSERT_TRUE(strcmp(g.e[0].path, "/x/y.flac") == 0 && strcmp(g.e[0].artist, "A") == 0);
}

TEST(windows_paths_drive_letters_and_file_urls) {
    char o[PL_PATH_MAX];
    ASSERT_TRUE(pl_resolve("/Playlists", "..\\Music\\a b.flac", o, sizeof o) && strcmp(o, "/Music/a b.flac") == 0);
    ASSERT_TRUE(pl_resolve("/", "C:\\Music\\x.flac", o, sizeof o) && strcmp(o, "/Music/x.flac") == 0); /* the drive is dropped */
    ASSERT_TRUE(pl_resolve("/p", "file:///Music/Caf%C3%A9/t%201.flac", o, sizeof o) && strcmp(o, "/Music/Caf\xC3\xA9/t 1.flac") == 0);
    ASSERT_TRUE(pl_resolve("/a/b", "./c/../d.mp3", o, sizeof o) && strcmp(o, "/a/b/d.mp3") == 0);
    ASSERT_TRUE(pl_resolve("/a/b", "/x//y/./z.wav", o, sizeof o) && strcmp(o, "/x/y/z.wav") == 0);
}

TEST(paths_that_cannot_name_a_place_on_the_card_stay_entries_but_are_unresolvable) {
    char o[PL_PATH_MAX];
    ASSERT_FALSE(pl_resolve("/p", "../../x.flac", o, sizeof o)); /* climbs above the root */
    ASSERT_FALSE(pl_resolve("/p", "http://host/a.mp3", o, sizeof o));
    ASSERT_FALSE(pl_resolve("/p", "C:song.mp3", o, sizeof o));
    ASSERT_FALSE(pl_resolve("/p", "/", o, sizeof o));
    got_t g;
    const char *t = "#EXTINF:5,T\nhttp://host/a.mp3\n../../up.flac\n/ok.flac\n";
    parse("/p", t, strlen(t), &g);
    ASSERT_EQ(3, g.n);
    ASSERT_FALSE(g.e[0].resolvable);
    ASSERT_TRUE(strcmp(g.e[0].title, "T") == 0); /* the title from #EXTINF still shows on the greyed row */
    ASSERT_FALSE(g.e[1].resolvable);
    ASSERT_TRUE(strcmp(g.e[1].title, "../../up.flac") == 0); /* otherwise what the line said */
    ASSERT_TRUE(g.e[2].resolvable);
}

TEST(a_line_that_is_not_utf8_is_read_as_latin1) {
    got_t g;
    const char *t = "/Music/Caf\xE9/Z\xFCrich.flac\n#EXTINF:1,B\xE9" "ni\n/Music/b.flac\n";
    parse("/", t, strlen(t), &g);
    ASSERT_EQ(2, g.n);
    ASSERT_TRUE(strcmp(g.e[0].path, "/Music/Caf\xC3\xA9/Z\xC3\xBCrich.flac") == 0);
    ASSERT_TRUE(strcmp(g.e[1].title, "B\xC3\xA9ni") == 0);
    const char *u = "/Music/Caf\xC3\xA9.flac\n"; /* valid UTF-8 stays as it is */
    parse("/", u, strlen(u), &g);
    ASSERT_TRUE(strcmp(g.e[0].path, "/Music/Caf\xC3\xA9.flac") == 0);
}

TEST(a_very_long_line_is_an_unresolvable_entry_not_a_crash) {
    char big[1200];
    memset(big, 'a', sizeof big);
    big[0] = '/';
    big[sizeof big - 1] = '\n';
    got_t g;
    parse("/", big, sizeof big, &g);
    ASSERT_EQ(1, g.n);
    ASSERT_FALSE(g.e[0].resolvable);
}

TEST(playlist_file_names) {
    ASSERT_TRUE(pl_is_playlist_name("a.m3u") && pl_is_playlist_name("Night drive.M3U8") && pl_is_playlist_name("x.m3u8"));
    ASSERT_FALSE(pl_is_playlist_name("a.mp3"));
    ASSERT_FALSE(pl_is_playlist_name("m3u"));
    ASSERT_FALSE(pl_is_playlist_name(".m3u9"));
    ASSERT_FALSE(pl_is_playlist_name("a.m3"));
    ASSERT_FALSE(pl_is_playlist_name("a.m3u8x"));
}

TEST(on_the_go_text_round_trips_through_the_reader) {
    char text[1024];
    size_t n = 0;
    memcpy(text, pl_m3u_header, strlen(pl_m3u_header));
    n = strlen(pl_m3u_header);
    n += pl_format_entry(text + n, sizeof text - n, "/Music/A/b.flac", "Song", "Artist", 183);
    n += pl_format_entry(text + n, sizeof text - n, "/Music/C/d.mp3", "Only title", "", 0);
    n += pl_format_entry(text + n, sizeof text - n, "/Music/E/f.wav", "Two\nlines", "Ar\rtist", 7); /* a line end cannot split an entry */
    got_t g;
    parse("/.neupod", text, n, &g);
    ASSERT_EQ(3, g.n);
    ASSERT_TRUE(strcmp(g.e[0].path, "/Music/A/b.flac") == 0 && strcmp(g.e[0].title, "Song") == 0 && strcmp(g.e[0].artist, "Artist") == 0 && g.e[0].secs == 183);
    ASSERT_TRUE(strcmp(g.e[1].title, "Only title") == 0 && g.e[1].artist[0] == '\0');
    ASSERT_TRUE(strcmp(g.e[2].path, "/Music/E/f.wav") == 0 && strcmp(g.e[2].title, "Two lines") == 0);
    char tiny[8];
    ASSERT_EQ(0, pl_format_entry(tiny, sizeof tiny, "/Music/A/b.flac", "Song", "Artist", 1));
}

/* ---- the queue built from paths ---------------------------------------------------------------------------------------------------------- */

TEST(a_queue_of_paths_plays_each_whole_path_and_names_the_file) {
    const char *paths[] = {"/Music/A/01.flac", "/Other/b.mp3", "/cover.jpg", "/Music/C/c.wav"};
    play_queue_t *q = calloc(1, sizeof *q);
    ASSERT_TRUE(pq_build_paths(q, paths, 4, 1));
    ASSERT_EQ(3, q->count); /* the .jpg is not audio */
    ASSERT_EQ(1, q->pos);
    char p[PLAY_QUEUE_PATH_MAX];
    ASSERT_TRUE(pq_path(q, 0, p, sizeof p) && strcmp(p, "/Music/A/01.flac") == 0);
    ASSERT_TRUE(pq_path(q, 1, p, sizeof p) && strcmp(p, "/Other/b.mp3") == 0);
    ASSERT_TRUE(pq_path(q, 2, p, sizeof p) && strcmp(p, "/Music/C/c.wav") == 0);
    ASSERT_TRUE(strcmp(pq_name(q, 1), "b.mp3") == 0); /* the player shows the file's own name */
    ASSERT_EQ(PQ_MOVED, pq_next(q));
    ASSERT_TRUE(pq_path(q, q->pos, p, sizeof p) && strcmp(p, "/Music/C/c.wav") == 0);
    ASSERT_EQ(PQ_END, pq_next(q));
    ASSERT_FALSE(pq_build_paths(q, paths, 4, 2)); /* not a music file */
    ASSERT_EQ(0, q->count);
    ASSERT_FALSE(pq_build_paths(q, paths, 4, 9));
    free(q);
}

TEST(a_long_playlist_keeps_a_window_around_the_chosen_entry) {
    enum { N = 400 };
    static char names[N][64];
    static const char *paths[N];
    for (int i = 0; i < N; i++) {
        snprintf(names[i], sizeof names[i], "/Music/Some Artist/Some Album/%03d Track.flac", i);
        paths[i] = names[i];
    }
    play_queue_t *q = calloc(1, sizeof *q);
    ASSERT_TRUE(pq_build_paths(q, paths, N, 200));
    ASSERT_TRUE(q->count <= PLAY_QUEUE_MAX_TRACKS && q->count > 20);
    char p[PLAY_QUEUE_PATH_MAX];
    ASSERT_TRUE(pq_path(q, q->pos, p, sizeof p) && strcmp(p, names[200]) == 0);
    ASSERT_TRUE(pq_has_next(q)); /* the entries after it are queued */
    free(q);
}

TEST(shuffle_applies_to_a_playlist_queue_and_keeps_the_chosen_track_first) {
    const char *paths[] = {"/a.flac", "/b.flac", "/c.flac", "/d.flac", "/e.flac"};
    play_queue_t *q = calloc(1, sizeof *q);
    q->shuffle = 1;
    pq_seed(q, 12345);
    ASSERT_TRUE(pq_build_paths(q, paths, 5, 3));
    char p[PLAY_QUEUE_PATH_MAX];
    ASSERT_TRUE(pq_path(q, 0, p, sizeof p) && strcmp(p, "/d.flac") == 0);
    bool seen[5] = {0};
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_TRUE(pq_path(q, i, p, sizeof p));
        seen[p[1] - 'a'] = true;
    }
    ASSERT_TRUE(seen[0] && seen[1] && seen[2] && seen[3] && seen[4]);
    free(q);
}

/* ---- when a track counts as played ----------------------------------------------------------------------------------------------------- */

TEST(a_play_counts_at_half_the_length_once) {
    ptrk_t t;
    ptrk_init(&t);
    uint32_t now = 1000;
    int counted = 0;
    for (int s = 0; s < 200; s++) { /* a 100 s track, one pass a second */
        now += 1000;
        counted += ptrk_update(&t, now, true, false, 7, 100000);
    }
    ASSERT_EQ(1, counted);
    ptrk_init(&t);
    counted = 0;
    now = 1000;
    for (int s = 0; s < 49; s++) { /* 49 s of a 100 s track: not yet */
        now += 1000;
        counted += ptrk_update(&t, now, true, false, 7, 100000);
    }
    ASSERT_EQ(0, counted);
}

TEST(pausing_a_seek_and_a_skip_do_not_count) {
    ptrk_t t;
    ptrk_init(&t);
    uint32_t now = 0;
    int counted = 0;
    for (int s = 0; s < 30; s++) {
        now += 1000;
        counted += ptrk_update(&t, now, false, false, 1, 100000); /* paused: the clock does not run */
    }
    ASSERT_EQ(0, counted);
    now += 1000;
    ASSERT_FALSE(ptrk_update(&t, now, true, false, 1, 100000));
    now += 3600u * 1000u; /* the task was held up for an hour: one pass adds at most a second */
    ASSERT_FALSE(ptrk_update(&t, now, true, false, 1, 100000));
    for (int s = 0; s < 20; s++) { /* 20 s of track 1, then it is skipped: a new id starts from nothing */
        now += 1000;
        counted += ptrk_update(&t, now, true, false, 1, 100000);
    }
    now += 1000;
    ASSERT_FALSE(ptrk_update(&t, now, true, false, 2, 100000));
    ASSERT_EQ(0, counted);
}

TEST(a_track_of_unknown_length_counts_after_30_s_and_one_that_ends_counts_after_3_s) {
    ptrk_t t;
    ptrk_init(&t);
    uint32_t now = 0;
    int counted = 0;
    for (int s = 0; s < 40; s++) {
        now += 1000;
        counted += ptrk_update(&t, now, true, false, 3, 0);
    }
    ASSERT_EQ(1, counted);
    ptrk_init(&t);
    now = 0;
    ASSERT_FALSE(ptrk_update(&t, now += 1000, true, false, 4, 0));
    ASSERT_FALSE(ptrk_update(&t, now += 1000, true, true, 4, 0)); /* ended after 1 s: too short to count */
    ASSERT_FALSE(ptrk_update(&t, now += 1000, true, false, 4, 0));
    ASSERT_TRUE(ptrk_update(&t, now += 1000, true, true, 4, 0)); /* ended, 3 s heard */
    ASSERT_FALSE(ptrk_update(&t, now += 1000, true, true, 4, 0)); /* once per start */
}

int main(void) {
    RUN(extinf_gives_title_artist_and_length_and_paths_resolve_against_the_playlists_folder);
    RUN(line_ends_and_a_byte_order_mark_and_a_missing_final_newline);
    RUN(a_feed_in_small_pieces_gives_the_same_entries);
    RUN(windows_paths_drive_letters_and_file_urls);
    RUN(paths_that_cannot_name_a_place_on_the_card_stay_entries_but_are_unresolvable);
    RUN(a_line_that_is_not_utf8_is_read_as_latin1);
    RUN(a_very_long_line_is_an_unresolvable_entry_not_a_crash);
    RUN(playlist_file_names);
    RUN(on_the_go_text_round_trips_through_the_reader);
    RUN(a_queue_of_paths_plays_each_whole_path_and_names_the_file);
    RUN(a_long_playlist_keeps_a_window_around_the_chosen_entry);
    RUN(shuffle_applies_to_a_playlist_queue_and_keeps_the_chosen_track_first);
    RUN(a_play_counts_at_half_the_length_once);
    RUN(pausing_a_seek_and_a_skip_do_not_count);
    RUN(a_track_of_unknown_length_counts_after_30_s_and_one_that_ends_counts_after_3_s);
    RESULT();
}
