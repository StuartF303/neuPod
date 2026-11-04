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
        uint16_t en = q->order[i];
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
    uint16_t cur = pq_entry(&q, q.pos);
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
        uint16_t en = pq_entry(&q, q.pos);
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
    uint16_t a = pq_entry(&q, 0);
    (void)pq_next(&q);
    uint16_t b = pq_entry(&q, q.pos);
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
    uint16_t en;
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
    uint16_t en;
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
        uint16_t last = pq_entry(&q, 39);
        uint16_t peek, peek2;
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
        uint16_t last = pq_entry(&q, 1);
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
    uint16_t offered = pq_entry(&q, q.pos + 1u);
    pq_set_shuffle(&q, PQ_SHUFFLE_OFF);
    ASSERT_EQ(PQ_MOVED, pq_commit(&q, offered));
    ASSERT_EQ(offered, q.pos);
    ASSERT_EQ(PQ_RESTART, pq_commit(&q, offered)); // the same track again: a repeat
    pq_set_repeat(&q, PQ_REPEAT_ALL);
    q.pos = 39;
    ASSERT_EQ(PQ_WRAPPED, pq_commit(&q, 0));
    ASSERT_EQ(0, q.pos);
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
    RUN(a_committed_entry_wins_over_a_changed_plan);
    return RESULT();
}
