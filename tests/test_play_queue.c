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

int main(void) {
    RUN(audio_names_are_recognised_case_insensitively);
    RUN(queue_is_the_folder_in_files_order_from_the_chosen_track);
    RUN(choosing_something_that_cannot_play_builds_nothing);
    RUN(next_walks_the_queue_and_stops_at_its_end);
    RUN(previous_restarts_after_three_seconds_otherwise_goes_back);
    RUN(a_folder_too_big_for_the_pool_keeps_a_window_around_the_chosen_track);
    RUN(a_single_track_folder_is_a_queue_of_one);
    return RESULT();
}
