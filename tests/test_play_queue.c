// SPDX-License-Identifier: MPL-2.0
// Play queue rules (app/runtime/play_queue.c): a folder is an album, in Files order, from the chosen track on.
#include <stdio.h>
#include <string.h>

#include "play_queue.h"
#include "unit.h"

static file_entry_t e(const char *name, bool dir) {
    file_entry_t x;
    memset(&x, 0, sizeof x);
    snprintf(x.name, sizeof x.name, "%s", name);
    x.is_dir = dir;
    return x;
}

// What the Files screen lists: folders first, then files alphabetically.
static file_entry_t g_dir[] = {
    {.name = "Artwork", .is_dir = true},   {.name = "Bonus", .is_dir = true},       {.name = "01 Intro.flac"},
    {.name = "02 Song.FLAC"},              {.name = "cover.jpg"},                   {.name = "03 Song.mp3"},
    {.name = "04 Song.m4a"},               {.name = "notes.csv"},                   {.name = "05 Live.wav"},
    {.name = "06 Video.mp4"},              {.name = "zz.txt"},
};
#define N (sizeof g_dir / sizeof g_dir[0])

TEST(audio_names_are_recognised_case_insensitively) {
    ASSERT_TRUE(pq_is_audio("a.flac"));
    ASSERT_TRUE(pq_is_audio("A.FLAC"));
    ASSERT_TRUE(pq_is_audio("x.Mp3"));
    ASSERT_TRUE(pq_is_audio("x.m4a"));
    ASSERT_TRUE(pq_is_audio("x.wav"));
    ASSERT_TRUE(pq_is_audio("x.mp4"));
    ASSERT_FALSE(pq_is_audio("cover.jpg"));
    ASSERT_FALSE(pq_is_audio("table.csv"));
    ASSERT_FALSE(pq_is_audio("flac"));
    ASSERT_FALSE(pq_is_audio("a.flac.txt"));
    ASSERT_FALSE(pq_is_audio(""));
}

TEST(queue_is_the_folder_in_files_order_from_the_chosen_track) {
    static play_queue_t q;
    // choose "03 Song.mp3" (index 5)
    ASSERT_TRUE(pq_build(&q, "/Music/Album", g_dir, N, 5));
    ASSERT_EQ(6, q.count); // 01, 02, 03, 04, 05, 06: folders, cover.jpg, notes.csv and zz.txt skipped
    ASSERT_EQ(2, q.pos);
    ASSERT_EQ(0, strcmp(pq_name(&q, 0), "01 Intro.flac"));
    ASSERT_EQ(0, strcmp(pq_name(&q, 1), "02 Song.FLAC"));
    ASSERT_EQ(0, strcmp(pq_name(&q, 2), "03 Song.mp3"));
    ASSERT_EQ(0, strcmp(pq_name(&q, 3), "04 Song.m4a"));
    ASSERT_EQ(0, strcmp(pq_name(&q, 4), "05 Live.wav"));
    ASSERT_EQ(0, strcmp(pq_name(&q, 5), "06 Video.mp4"));
    ASSERT_TRUE(pq_name(&q, 6) == NULL);
    char path[300];
    ASSERT_TRUE(pq_path(&q, q.pos, path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Music/Album/03 Song.mp3"));
    // in the root folder there is no doubled slash
    ASSERT_TRUE(pq_build(&q, "/", g_dir, N, 2));
    ASSERT_TRUE(pq_path(&q, 0, path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/01 Intro.flac"));
    // a path that does not fit is refused, not cut
    char small[8];
    ASSERT_FALSE(pq_path(&q, 0, small, sizeof small));
}

TEST(choosing_something_that_cannot_play_builds_nothing) {
    static play_queue_t q;
    ASSERT_FALSE(pq_build(&q, "/", g_dir, N, 0));  // a folder
    ASSERT_FALSE(pq_build(&q, "/", g_dir, N, 4));  // cover.jpg
    ASSERT_FALSE(pq_build(&q, "/", g_dir, N, 99)); // out of range
    ASSERT_EQ(0, q.count);
    ASSERT_FALSE(pq_has_next(&q));
}

TEST(next_walks_the_queue_and_stops_at_its_end) {
    static play_queue_t q;
    ASSERT_TRUE(pq_build(&q, "/a", g_dir, N, 8)); // "05 Live.wav": index 8 -> queue 01..06, pos 4
    ASSERT_EQ(6, q.count);
    ASSERT_EQ(4, q.pos);
    ASSERT_TRUE(pq_has_next(&q));
    ASSERT_EQ(PQ_MOVED, pq_next(&q));
    ASSERT_EQ(5, q.pos);
    ASSERT_FALSE(pq_has_next(&q));
    ASSERT_EQ(PQ_END, pq_next(&q)); // the end of the queue: position stays, the player stops
    ASSERT_EQ(5, q.pos);
}

TEST(previous_restarts_after_three_seconds_otherwise_goes_back) {
    static play_queue_t q;
    ASSERT_TRUE(pq_build(&q, "/a", g_dir, N, 5));
    ASSERT_EQ(2, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 4));  // more than 3 s in: start this track again
    ASSERT_EQ(2, q.pos);
    ASSERT_EQ(PQ_MOVED, pq_prev(&q, 3));    // exactly 3 s counts as "early": go back
    ASSERT_EQ(1, q.pos);
    ASSERT_EQ(PQ_MOVED, pq_prev(&q, 0));
    ASSERT_EQ(0, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 0));  // first track: restart rather than wrap
    ASSERT_EQ(0, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 100));
    ASSERT_EQ(0, q.pos);
}

TEST(a_folder_too_big_for_the_pool_keeps_a_window_around_the_chosen_track) {
    static file_entry_t big[200];
    for (int i = 0; i < 200; i++) {
        big[i] = e("", false);
        snprintf(big[i].name, sizeof big[i].name, "%03d - a rather long track title number %03d.flac", i, i); // 46 bytes each
    }
    static play_queue_t q;
    ASSERT_TRUE(pq_build(&q, "/big", big, 200, 150));
    ASSERT_TRUE(q.count > 0 && q.count <= PLAY_QUEUE_MAX_TRACKS);
    ASSERT_TRUE(q.count * 47u <= PLAY_QUEUE_POOL_BYTES);
    // the chosen track is in it, everything after it that fits follows in order, and the names are the right ones
    ASSERT_EQ(0, strcmp(pq_name(&q, q.pos), big[150].name));
    for (uint32_t i = q.pos; i + 1 < q.count; i++) {
        ASSERT_TRUE(strcmp(pq_name(&q, i), pq_name(&q, i + 1)) < 0);
    }
    // choosing the first track of a huge folder keeps it first
    ASSERT_TRUE(pq_build(&q, "/big", big, 200, 0));
    ASSERT_EQ(0, q.pos);
    ASSERT_EQ(0, strcmp(pq_name(&q, 0), big[0].name));
    // choosing near the end keeps the end
    ASSERT_TRUE(pq_build(&q, "/big", big, 200, 199));
    ASSERT_EQ(0, strcmp(pq_name(&q, q.count - 1), big[199].name));
    ASSERT_EQ(q.count - 1, q.pos);
}

TEST(a_single_track_folder_is_a_queue_of_one) {
    static play_queue_t q;
    file_entry_t one[] = {{.name = "only.flac"}, {.name = "cover.jpg"}};
    ASSERT_TRUE(pq_build(&q, "/x", one, 2, 0));
    ASSERT_EQ(1, q.count);
    ASSERT_FALSE(pq_has_next(&q));
    ASSERT_EQ(PQ_END, pq_next(&q));
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 0));
    pq_clear(&q);
    ASSERT_EQ(0, q.count);
}

// ---- shuffle / repeat -----------------------------------------------------------------------------------------------------

static bool is_permutation(const play_queue_t *q) {
    bool seen[PLAY_QUEUE_MAX_TRACKS] = {0};
    for (uint32_t i = 0; i < q->count; i++) {
        uint32_t en = q->order[i];
        if (en >= q->count || seen[en]) {
            return false;
        }
        seen[en] = true;
    }
    return true;
}

static file_entry_t g_many[40];
static void many(void) {
    for (int i = 0; i < 40; i++) {
        g_many[i] = e("", false);
        snprintf(g_many[i].name, sizeof g_many[i].name, "%02d.flac", i);
    }
}

TEST(shuffle_is_a_valid_permutation_with_the_chosen_track_first) {
    many();
    static play_queue_t q;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        pq_clear(&q);
        pq_seed(&q, seed);
        pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
        ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, seed % 40));
        ASSERT_EQ(40, q.count);
        ASSERT_TRUE(is_permutation(&q));
        ASSERT_EQ(0, q.pos);
        ASSERT_EQ(seed % 40, pq_entry(&q, 0)); // the track the user chose plays first
    }
    // the same seed gives the same order, another seed another one
    static play_queue_t a, b, c;
    pq_clear(&a); pq_clear(&b); pq_clear(&c);
    pq_seed(&a, 7); pq_set_shuffle(&a, PQ_SHUFFLE_SONGS); ASSERT_TRUE(pq_build(&a, "/a", g_many, 40, 0));
    pq_seed(&b, 7); pq_set_shuffle(&b, PQ_SHUFFLE_SONGS); ASSERT_TRUE(pq_build(&b, "/a", g_many, 40, 0));
    pq_seed(&c, 8); pq_set_shuffle(&c, PQ_SHUFFLE_SONGS); ASSERT_TRUE(pq_build(&c, "/a", g_many, 40, 0));
    ASSERT_EQ(0, memcmp(a.order, b.order, 40 * sizeof a.order[0]));
    ASSERT_TRUE(memcmp(a.order, c.order, 40 * sizeof a.order[0]) != 0);
    // paths follow the permutation
    char path[300], want[300];
    ASSERT_TRUE(pq_path(&a, 3, path, sizeof path));
    snprintf(want, sizeof want, "/a/%02d.flac", a.order[3]);
    ASSERT_EQ(0, strcmp(path, want));
}

TEST(shuffle_actually_mixes) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 12345);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 0));
    int in_place = 0;
    for (uint32_t i = 0; i < 40; i++) {
        in_place += q.order[i] == i;
    }
    ASSERT_TRUE(in_place < 10);
}

TEST(toggling_shuffle_mid_play_keeps_the_current_track) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 99);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 17));
    ASSERT_EQ(17, q.pos);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    ASSERT_TRUE(is_permutation(&q));
    ASSERT_EQ(17, pq_entry(&q, q.pos));
    ASSERT_EQ(0, strcmp(pq_name(&q, q.pos), "17.flac"));
    // walk some tracks, then switch off: back to folder order, same track playing
    ASSERT_EQ(PQ_MOVED, pq_next(&q));
    ASSERT_EQ(PQ_MOVED, pq_next(&q));
    uint32_t cur = pq_entry(&q, q.pos);
    pq_set_shuffle(&q, PQ_SHUFFLE_OFF);
    ASSERT_EQ(cur, q.pos);
    for (uint32_t i = 0; i < 40; i++) {
        ASSERT_EQ(i, q.order[i]);
    }
    pq_set_shuffle(&q, PQ_SHUFFLE_OFF); // same mode again: nothing changes
    ASSERT_EQ(cur, q.pos);
}

TEST(shuffle_plays_every_track_once_then_ends_without_repeat) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 5);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 3));
    bool seen[40] = {0};
    seen[pq_entry(&q, q.pos)] = true;
    for (int i = 1; i < 40; i++) {
        ASSERT_EQ(PQ_MOVED, pq_auto_next(&q));
        uint32_t en = pq_entry(&q, q.pos);
        ASSERT_FALSE(seen[en]);
        seen[en] = true;
    }
    ASSERT_EQ(PQ_END, pq_auto_next(&q));
    ASSERT_EQ(39, q.pos);
}

TEST(previous_walks_back_through_the_shuffled_order) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 5);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 3));
    uint32_t a = pq_entry(&q, 0);
    (void)pq_next(&q);
    uint32_t b = pq_entry(&q, q.pos);
    (void)pq_next(&q);
    ASSERT_EQ(PQ_MOVED, pq_prev(&q, 1));
    ASSERT_EQ(b, pq_entry(&q, q.pos));
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 4)); // more than 3 s in: restart
    ASSERT_EQ(b, pq_entry(&q, q.pos));
    ASSERT_EQ(PQ_MOVED, pq_prev(&q, 0));
    ASSERT_EQ(a, pq_entry(&q, q.pos));
    ASSERT_EQ(PQ_RESTART, pq_prev(&q, 0)); // first slot
}

TEST(repeat_one_loops_automatic_advances_but_next_still_moves_on) {
    static play_queue_t q;
    pq_clear(&q);
    ASSERT_TRUE(pq_build(&q, "/a", g_dir, N, 5));
    pq_set_repeat(&q, PQ_REPEAT_ONE);
    ASSERT_EQ(2, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_auto_next(&q));
    ASSERT_EQ(PQ_RESTART, pq_auto_next(&q));
    ASSERT_EQ(2, q.pos);
    uint32_t en;
    ASSERT_TRUE(pq_peek_auto(&q, &en)); // the gapless look-ahead is the same track
    ASSERT_EQ(2, en);
    ASSERT_EQ(PQ_MOVED, pq_next(&q));   // the button
    ASSERT_EQ(3, q.pos);
    q.pos = 5; // on the last track too
    ASSERT_EQ(PQ_RESTART, pq_auto_next(&q));
    ASSERT_EQ(PQ_END, pq_next(&q));
    pq_clear(&q); // the mode survives clearing and rebuilding
    ASSERT_EQ(PQ_REPEAT_ONE, q.repeat);
}

TEST(repeat_all_wraps_to_the_first_track) {
    static play_queue_t q;
    pq_clear(&q);
    ASSERT_TRUE(pq_build(&q, "/a", g_dir, N, 8)); // 6 tracks, at slot 4
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    uint32_t en;
    ASSERT_TRUE(pq_peek_auto(&q, &en));
    ASSERT_EQ(5, en);
    ASSERT_EQ(PQ_MOVED, pq_auto_next(&q));
    ASSERT_TRUE(pq_peek_auto(&q, &en));
    ASSERT_EQ(0, en); // the look-ahead at the end is the first track
    ASSERT_EQ(PQ_WRAPPED, pq_auto_next(&q));
    ASSERT_EQ(0, q.pos);
    pq_set_repeat(&q, PQ_REPEAT_OFF); // repeat off: the end is the end
    q.pos = 5;
    ASSERT_FALSE(pq_peek_auto(&q, &en));
    ASSERT_EQ(PQ_END, pq_auto_next(&q));
    ASSERT_EQ(5, q.pos);
    file_entry_t one[] = {{.name = "only.flac"}}; // a one-track queue repeats itself
    ASSERT_TRUE(pq_build(&q, "/x", one, 1, 0));
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    ASSERT_EQ(PQ_WRAPPED, pq_auto_next(&q));
    ASSERT_EQ(0, q.pos);
}

TEST(repeat_all_with_shuffle_reshuffles_and_never_opens_with_the_last_track) {
    many();
    static play_queue_t q;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        pq_clear(&q);
        pq_seed(&q, seed);
        pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
        pq_set_repeat(&q, PQ_REPEAT_ALL);
        ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 0));
        q.pos = 39;
        uint32_t last = pq_entry(&q, 39);
        uint32_t peek, peek2;
        ASSERT_TRUE(pq_peek_auto(&q, &peek));
        ASSERT_TRUE(peek != last);
        ASSERT_TRUE(pq_peek_auto(&q, &peek2)); // stable until the move is made
        ASSERT_EQ(peek, peek2);
        ASSERT_EQ(PQ_WRAPPED, pq_auto_next(&q));
        ASSERT_EQ(0, q.pos);
        ASSERT_EQ(peek, pq_entry(&q, 0)); // the track the gapless look-ahead promised
        ASSERT_TRUE(is_permutation(&q));
    }
    // two tracks: the new round must not open with the track that just ended
    file_entry_t two[] = {{.name = "a.flac"}, {.name = "b.flac"}};
    pq_clear(&q);
    pq_seed(&q, 3);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    ASSERT_TRUE(pq_build(&q, "/x", two, 2, 0));
    for (int i = 0; i < 20; i++) {
        uint32_t last = pq_entry(&q, 1);
        ASSERT_EQ(PQ_MOVED, pq_auto_next(&q));
        ASSERT_EQ(PQ_WRAPPED, pq_auto_next(&q));
        ASSERT_TRUE(pq_entry(&q, 0) != last);
        ASSERT_TRUE(is_permutation(&q));
    }
}

TEST(a_committed_entry_wins_over_a_changed_plan) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 11);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 4));
    // folder order, the audio core was told "5", then shuffle is switched on
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    ASSERT_EQ(PQ_MOVED, pq_commit(&q, 5));
    ASSERT_EQ(5, pq_entry(&q, q.pos));
    ASSERT_EQ(1, q.pos);
    ASSERT_TRUE(is_permutation(&q));
    // the other way round: shuffle off after the offer
    uint32_t offered = pq_entry(&q, q.pos + 1u);
    pq_set_shuffle(&q, PQ_SHUFFLE_OFF);
    ASSERT_EQ(PQ_MOVED, pq_commit(&q, offered));
    ASSERT_EQ(offered, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_commit(&q, offered)); // the same track again: a repeat
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    q.pos = 39;
    ASSERT_EQ(PQ_WRAPPED, pq_commit(&q, 0));
    ASSERT_EQ(0, q.pos);
}

TEST(snapshot_restores_order_position_and_seed_after_a_wrap) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 21);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 0));
    q.pos = 39;
    static pq_snapshot_t s;
    static uint16_t order[40];
    memcpy(order, q.order, sizeof order);
    uint32_t cur = pq_entry(&q, q.pos);
    pq_snapshot(&q, &s);
    ASSERT_EQ(PQ_WRAPPED, pq_next(&q));
    ASSERT_TRUE(memcmp(order, q.order, sizeof order) != 0);
    pq_restore(&q, &s);
    ASSERT_EQ(0, memcmp(order, q.order, sizeof order));
    ASSERT_EQ(39, q.pos);
    ASSERT_EQ(cur, pq_entry(&q, q.pos));
}

TEST(commit_of_an_entry_at_or_behind_the_position_is_pulled_in_as_the_next_track) {
    many();
    static play_queue_t q;
    pq_clear(&q);
    pq_seed(&q, 4);
    ASSERT_TRUE(pq_build(&q, "/a", g_many, 40, 10));
    ASSERT_EQ(PQ_MOVED, pq_commit(&q, 3)); // folder order: jumps to it
    ASSERT_EQ(3, q.pos);
    pq_set_shuffle(&q, PQ_SHUFFLE_SONGS);  // shuffled: entry 3 is current at slot 0; commit an entry that sat earlier in the walk
    ASSERT_EQ(PQ_MOVED, pq_next(&q));
    uint32_t earlier = pq_entry(&q, 0);
    ASSERT_EQ(PQ_MOVED, pq_commit(&q, earlier)); // behind pos: it becomes the next slot, the permutation stays valid
    ASSERT_EQ(2, q.pos);
    ASSERT_EQ(earlier, pq_entry(&q, 2));
    ASSERT_TRUE(is_permutation(&q));
    ASSERT_EQ(PQ_END, pq_commit(&q, 99));  // not an entry: nothing happens
}


/* ---- library range queues ------------------------------------------------------------------------------------------ */

static play_queue_t g_lq;
static const pq_range_t k_album = {.order = 1, .first = 40, .count = 12};

// Walk the whole queue with Next from the current slot: the entries seen, in order, once each.
static bool walks_every_entry_once(play_queue_t *q, uint32_t n) {
    static uint8_t seen[70000];
    memset(seen, 0, n);
    uint32_t count = 1;
    seen[pq_entry(q, q->pos)] = 1;
    if (q->pos != 0 && !q->shuffle) {
        return false; // the helper starts at the first slot
    }
    while (pq_next(q) == PQ_MOVED) {
        uint32_t e = pq_entry(q, q->pos);
        if (e >= n || seen[e]) {
            return false;
        }
        seen[e] = 1;
        count++;
    }
    return count == n;
}

TEST(library_queue_handles_are_formatted_and_parsed) {
    char h[40];
    ASSERT_TRUE(pq_format_handle(h, sizeof h, 2, 123456));
    ASSERT_EQ(0, strcmp(h, "@lib/2/123456"));
    ASSERT_TRUE(pq_is_handle(h));
    ASSERT_FALSE(pq_is_handle("/Music/a.flac"));
    ASSERT_FALSE(pq_is_handle(NULL));
    uint8_t o = 9;
    uint32_t p = 0;
    ASSERT_TRUE(pq_parse_handle(h, &o, &p));
    ASSERT_EQ(2, o);
    ASSERT_EQ(123456, p);
    ASSERT_TRUE(pq_parse_handle("@lib/0/4294967295", &o, &p));
    ASSERT_EQ(0xFFFFFFFFu, p);
    ASSERT_FALSE(pq_parse_handle("@lib/0/4294967296", &o, &p)); // does not fit 32 bits
    ASSERT_FALSE(pq_parse_handle("@lib/", &o, &p));
    ASSERT_FALSE(pq_parse_handle("@lib/1", &o, &p));
    ASSERT_FALSE(pq_parse_handle("@lib/1/", &o, &p));
    ASSERT_FALSE(pq_parse_handle("@lib/1/2x", &o, &p));
    ASSERT_FALSE(pq_parse_handle("@lib/300/2", &o, &p));
    ASSERT_FALSE(pq_parse_handle("@lib/x/2", &o, &p));
    ASSERT_FALSE(pq_format_handle(h, 6, 1, 1)); // too small
}

TEST(library_queue_plays_the_range_in_order_from_the_chosen_song) {
    pq_clear(&g_lq);
    g_lq.shuffle = 0;
    ASSERT_TRUE(pq_build_range(&g_lq, &k_album, 3));
    ASSERT_EQ(12, g_lq.count);
    ASSERT_EQ(3, g_lq.pos);
    ASSERT_TRUE(g_lq.lib);
    char path[40];
    ASSERT_TRUE(pq_path(&g_lq, g_lq.pos, path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "@lib/1/43")); // first + index: a position in the order table
    ASSERT_TRUE(pq_path(&g_lq, 0, path, sizeof path)); // the songs before it stay queued for Previous
    ASSERT_EQ(0, strcmp(path, "@lib/1/40"));
    ASSERT_FALSE(pq_path(&g_lq, 12, path, sizeof path));
    ASSERT_EQ(0, strcmp(pq_name(&g_lq, 3), "")); // no name until the storage task has looked it up
    ASSERT_TRUE(pq_has_next(&g_lq));
    for (int i = 0; i < 8; i++) ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    ASSERT_EQ(11, g_lq.pos);
    ASSERT_EQ(PQ_END, pq_next(&g_lq));
    ASSERT_EQ(11, g_lq.pos); // unchanged at the end
    ASSERT_EQ(PQ_MOVED, pq_prev(&g_lq, 0));
    ASSERT_EQ(10, g_lq.pos);
    ASSERT_EQ(PQ_RESTART, pq_prev(&g_lq, 10)); // more than 3 s in: the same song again
}

TEST(library_queue_rejects_empty_oversized_and_out_of_range) {
    pq_clear(&g_lq);
    ASSERT_FALSE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 0}, 0));
    ASSERT_EQ(0, g_lq.count);
    ASSERT_FALSE(pq_build_range(&g_lq, &k_album, 12));
    ASSERT_FALSE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = PLAY_QUEUE_LIB_MAX + 1u}, 0));
    ASSERT_FALSE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0xFFFFFFF0u, .count = 100}, 0)); // first + count wraps
    ASSERT_FALSE(pq_build_range(&g_lq, NULL, 0));
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = PLAY_QUEUE_LIB_MAX}, 5)); // 2 billion songs cost the same
}

TEST(library_shuffle_visits_every_song_once_with_the_chosen_one_first) {
    static const uint32_t sizes[] = {1, 2, 3, 4, 5, 7, 16, 17, 100, 255, 256, 257, 1000, 4097, 65537};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t n = sizes[i];
        pq_clear(&g_lq);
        pq_seed(&g_lq, 1234u + n);
        g_lq.shuffle = 1;
        uint32_t start = n / 2;
        ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 10, .count = n}, start));
        ASSERT_EQ(0, g_lq.pos);
        ASSERT_EQ(start, pq_entry(&g_lq, 0));
        ASSERT_TRUE(walks_every_entry_once(&g_lq, n));
    }
}

TEST(library_shuffle_really_mixes_and_depends_on_the_seed) {
    pq_clear(&g_lq);
    pq_seed(&g_lq, 99);
    g_lq.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 1000}, 0));
    uint32_t in_place = 0, ascending = 0;
    for (uint32_t i = 1; i < 1000; i++) {
        in_place += pq_entry(&g_lq, i) == i;
        ascending += pq_entry(&g_lq, i) > pq_entry(&g_lq, i - 1);
    }
    ASSERT_TRUE(in_place < 20);                       // a random permutation fixes about one
    ASSERT_TRUE(ascending > 400 && ascending < 600);  // and goes up about half of the time
    static play_queue_t other;
    pq_clear(&other);
    pq_seed(&other, 12345);
    other.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&other, &(pq_range_t){.order = 0, .first = 0, .count = 1000}, 0));
    uint32_t same = 0;
    for (uint32_t i = 1; i < 1000; i++) same += pq_entry(&g_lq, i) == pq_entry(&other, i);
    ASSERT_TRUE(same < 20); // another seed, another order
}

TEST(library_start_may_be_random_for_shuffle_songs) {
    uint32_t firsts = 0, last = 0;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        pq_clear(&g_lq);
        pq_seed(&g_lq, seed * 7919u);
        g_lq.shuffle = 1;
        ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 500}, PLAY_QUEUE_NO_ENTRY));
        uint32_t first = pq_entry(&g_lq, 0);
        ASSERT_TRUE(first < 500);
        firsts += first != last;
        last = first;
    }
    ASSERT_TRUE(firsts > 30); // not always the same opening song
}

TEST(library_toggling_shuffle_keeps_the_current_song_and_back_again) {
    pq_clear(&g_lq);
    pq_seed(&g_lq, 5);
    g_lq.shuffle = 0;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 1, .first = 0, .count = 300}, 120));
    pq_set_shuffle(&g_lq, PQ_SHUFFLE_SONGS);
    ASSERT_EQ(0, g_lq.pos);
    ASSERT_EQ(120, pq_entry(&g_lq, 0)); // still the same song
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    uint32_t cur = pq_entry(&g_lq, g_lq.pos);
    pq_set_shuffle(&g_lq, PQ_SHUFFLE_OFF);
    ASSERT_EQ(cur, g_lq.pos); // folder order again, at the song that plays
    ASSERT_EQ(cur, pq_entry(&g_lq, g_lq.pos));
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    ASSERT_EQ(cur + 1u, g_lq.pos);
}

TEST(library_previous_walks_back_through_the_shuffled_order) {
    pq_clear(&g_lq);
    pq_seed(&g_lq, 31);
    g_lq.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 50}, 7));
    uint32_t a = pq_entry(&g_lq, 0);
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    uint32_t b = pq_entry(&g_lq, g_lq.pos);
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
    ASSERT_EQ(PQ_MOVED, pq_prev(&g_lq, 0));
    ASSERT_EQ(b, pq_entry(&g_lq, g_lq.pos));
    ASSERT_EQ(PQ_MOVED, pq_prev(&g_lq, 0));
    ASSERT_EQ(a, pq_entry(&g_lq, g_lq.pos));
    ASSERT_EQ(PQ_RESTART, pq_prev(&g_lq, 0)); // the first slot: restart
}

TEST(library_repeat_all_wraps_and_a_shuffled_round_never_opens_with_the_last_song) {
    pq_clear(&g_lq);
    pq_seed(&g_lq, 77);
    pq_set_repeat(&g_lq, PQ_REPEAT_ALL);
    g_lq.shuffle = 0;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 5}, 4));
    ASSERT_EQ(PQ_WRAPPED, pq_next(&g_lq));
    ASSERT_EQ(0, g_lq.pos); // unshuffled: back to the start of the range
    g_lq.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 20}, 3));
    for (int round = 0; round < 30; round++) {
        while (pq_has_next(&g_lq)) ASSERT_EQ(PQ_MOVED, pq_next(&g_lq));
        uint32_t last = pq_entry(&g_lq, g_lq.pos);
        uint32_t offered;
        ASSERT_TRUE(pq_peek_auto(&g_lq, &offered)); // the gapless look-ahead fixes the opener of the next round ...
        ASSERT_TRUE(offered != last);
        ASSERT_EQ(PQ_WRAPPED, pq_auto_next(&g_lq));
        ASSERT_EQ(offered, pq_entry(&g_lq, 0)); // ... and it is what plays
        ASSERT_EQ(0, g_lq.pos);
    }
}

TEST(library_repeat_one_and_the_gapless_look_ahead) {
    pq_clear(&g_lq);
    g_lq.shuffle = 0;
    pq_set_repeat(&g_lq, PQ_REPEAT_OFF);
    ASSERT_TRUE(pq_build_range(&g_lq, &k_album, 5));
    uint32_t en;
    ASSERT_TRUE(pq_peek_auto(&g_lq, &en));
    ASSERT_EQ(6, en);
    pq_set_repeat(&g_lq, PQ_REPEAT_ONE);
    ASSERT_TRUE(pq_peek_auto(&g_lq, &en));
    ASSERT_EQ(5, en);
    ASSERT_EQ(PQ_RESTART, pq_auto_next(&g_lq));
    ASSERT_EQ(PQ_MOVED, pq_next(&g_lq)); // a press still moves on
    pq_set_repeat(&g_lq, PQ_REPEAT_OFF);
    g_lq.pos = 11;
    ASSERT_FALSE(pq_peek_auto(&g_lq, &en)); // last song, no repeat: nothing follows
}

TEST(library_commit_follows_the_plan_or_rebases_on_the_song_that_played) {
    pq_clear(&g_lq);
    g_lq.shuffle = 0;
    pq_set_repeat(&g_lq, PQ_REPEAT_OFF);
    ASSERT_TRUE(pq_build_range(&g_lq, &k_album, 2));
    ASSERT_EQ(PQ_MOVED, pq_commit(&g_lq, 3)); // the planned successor
    ASSERT_EQ(3, g_lq.pos);
    ASSERT_EQ(PQ_RESTART, pq_commit(&g_lq, 3));
    ASSERT_EQ(PQ_MOVED, pq_commit(&g_lq, 9)); // the plan changed (modes) before the core handed off: follow the core
    ASSERT_EQ(9, g_lq.pos);
    ASSERT_EQ(PQ_END, pq_commit(&g_lq, 12));
    pq_seed(&g_lq, 3);
    g_lq.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 100}, 10));
    uint32_t planned;
    ASSERT_TRUE(pq_peek_auto(&g_lq, &planned));
    ASSERT_EQ(PQ_MOVED, pq_commit(&g_lq, planned));
    ASSERT_EQ(1, g_lq.pos);
    uint32_t other = (pq_entry(&g_lq, 5) + 1u) % 100u; // something that is not the plan
    if (other == pq_entry(&g_lq, 2)) other = (other + 1u) % 100u;
    ASSERT_EQ(PQ_MOVED, pq_commit(&g_lq, other));
    ASSERT_EQ(other, pq_entry(&g_lq, g_lq.pos)); // it is the current song now
    g_lq.pos = 0;
    ASSERT_TRUE(g_lq.pos == 0); // (slot 0 is the song that played: a walk from here sees every song once)
    ASSERT_TRUE(walks_every_entry_once(&g_lq, 100));
}

TEST(library_snapshot_puts_a_wrapped_shuffle_back) {
    pq_clear(&g_lq);
    pq_seed(&g_lq, 8);
    pq_set_repeat(&g_lq, PQ_REPEAT_ALL);
    g_lq.shuffle = 1;
    ASSERT_TRUE(pq_build_range(&g_lq, &(pq_range_t){.order = 0, .first = 0, .count = 30}, 4));
    while (pq_has_next(&g_lq)) (void)pq_next(&g_lq);
    uint32_t last = pq_entry(&g_lq, g_lq.pos), first = pq_entry(&g_lq, 0);
    pq_snapshot_t s;
    pq_snapshot(&g_lq, &s);
    ASSERT_EQ(PQ_WRAPPED, pq_next(&g_lq));
    ASSERT_TRUE(g_lq.lib_key != s.lib_key); // the order was replaced by the wrap
    pq_restore(&g_lq, &s);
    ASSERT_EQ(last, pq_entry(&g_lq, g_lq.pos));
    ASSERT_EQ(first, pq_entry(&g_lq, 0));
    pq_set_repeat(&g_lq, PQ_REPEAT_OFF);
}

TEST(a_folder_queue_after_a_library_queue_is_a_folder_queue) {
    pq_clear(&g_lq);
    g_lq.shuffle = 0;
    ASSERT_TRUE(pq_build_range(&g_lq, &k_album, 0));
    ASSERT_TRUE(g_lq.lib);
    ASSERT_TRUE(pq_build(&g_lq, "/a", g_dir, N, 2));
    ASSERT_FALSE(g_lq.lib);
    char path[64];
    ASSERT_TRUE(pq_path(&g_lq, g_lq.pos, path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/a/01 Intro.flac"));
    pq_clear(&g_lq);
    ASSERT_FALSE(g_lq.lib);
}

int main(void) {
    RUN(audio_names_are_recognised_case_insensitively);
    RUN(queue_is_the_folder_in_files_order_from_the_chosen_track);
    RUN(choosing_something_that_cannot_play_builds_nothing);
    RUN(next_walks_the_queue_and_stops_at_its_end);
    RUN(previous_restarts_after_three_seconds_otherwise_goes_back);
    RUN(a_folder_too_big_for_the_pool_keeps_a_window_around_the_chosen_track);
    RUN(a_single_track_folder_is_a_queue_of_one);
    RUN(shuffle_is_a_valid_permutation_with_the_chosen_track_first);
    RUN(shuffle_actually_mixes);
    RUN(toggling_shuffle_mid_play_keeps_the_current_track);
    RUN(shuffle_plays_every_track_once_then_ends_without_repeat);
    RUN(previous_walks_back_through_the_shuffled_order);
    RUN(repeat_one_loops_automatic_advances_but_next_still_moves_on);
    RUN(repeat_all_wraps_to_the_first_track);
    RUN(repeat_all_with_shuffle_reshuffles_and_never_opens_with_the_last_track);
    RUN(snapshot_restores_order_position_and_seed_after_a_wrap);
    RUN(commit_of_an_entry_at_or_behind_the_position_is_pulled_in_as_the_next_track);
    RUN(a_committed_entry_wins_over_a_changed_plan);
    RUN(library_queue_handles_are_formatted_and_parsed);
    RUN(library_queue_plays_the_range_in_order_from_the_chosen_song);
    RUN(library_queue_rejects_empty_oversized_and_out_of_range);
    RUN(library_shuffle_visits_every_song_once_with_the_chosen_one_first);
    RUN(library_shuffle_really_mixes_and_depends_on_the_seed);
    RUN(library_start_may_be_random_for_shuffle_songs);
    RUN(library_toggling_shuffle_keeps_the_current_song_and_back_again);
    RUN(library_previous_walks_back_through_the_shuffled_order);
    RUN(library_repeat_all_wraps_and_a_shuffled_round_never_opens_with_the_last_song);
    RUN(library_repeat_one_and_the_gapless_look_ahead);
    RUN(library_commit_follows_the_plan_or_rebases_on_the_song_that_played);
    RUN(library_snapshot_puts_a_wrapped_shuffle_back);
    RUN(a_folder_queue_after_a_library_queue_is_a_folder_queue);
    return RESULT();
}
