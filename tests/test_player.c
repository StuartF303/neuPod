// SPDX-License-Identifier: MPL-2.0
// Player (app/runtime/player.c) against fakes for the audio core, the storage task and the settings store: the queue position and the
// gapless announcement bookkeeping around skips, hard starts and handoffs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"
#include "player.h"
#include "settings_store.h"
#include "unit.h"

// ---- fakes ------------------------------------------------------------------------------------------------------------
static uint32_t g_changes, g_ended;
static bool g_play_ok = true;
static int g_play_calls;
static char g_last_path[300];
static uint8_t g_gapless_setting = 1, g_shuffle_setting, g_repeat_setting;
uint32_t audio_track_change_count(void) { return g_changes; }
uint32_t audio_pcm_ended_count(void) { return g_ended; }
bool audio_stats_get(audio_stats_t *o) {
    memset(o, 0, sizeof *o);
    o->source = AUDIO_SRC_PCM;
    o->pcm_frames = 44100u * 10u;
    return true;
}
bool audio_set_volume(uint32_t v) { (void)v; return true; }
bool audio_pause(bool p) { (void)p; return true; }
bool audio_stop(void) { return true; }
bool audio_tone(uint32_t m) { (void)m; return true; }
bool storage_play_request(const char *path) {
    g_play_calls++;
    snprintf(g_last_path, sizeof g_last_path, "%s", path);
    return g_play_ok;
}
bool storage_verify_request(const char *p) { (void)p; return true; }
bool storage_stop_request(void) { return true; }
static uint8_t *slot(uint32_t tag) { return tag == SETTING_SHUFFLE ? &g_shuffle_setting : tag == SETTING_REPEAT ? &g_repeat_setting : &g_gapless_setting; }
bool settings_get_u8(uint32_t tag, uint8_t *out) { *out = *slot(tag); return true; }
void settings_set_u8(uint32_t tag, uint8_t v) { *slot(tag) = v; }
struct os_mutex { int x; };
os_mutex_t *os_mutex_create(void) { return calloc(1, sizeof(struct os_mutex)); }
bool os_mutex_lock(os_mutex_t *m, uint32_t t) { (void)m; (void)t; return true; }
void os_mutex_unlock(os_mutex_t *m) { (void)m; }
uint32_t os_tick_ms(void) { static uint32_t t; return t += 1000; } // always "long after" the start grace

// ---- helpers -----------------------------------------------------------------------------------------------------------
static file_entry_t g_dir[] = {{.name = "01.flac"}, {.name = "02.flac"}, {.name = "03.flac"}, {.name = "04.flac"}};
static player_file_t file(const char *title) {
    player_file_t f;
    memset(&f, 0, sizeof f);
    f.rate = 44100;
    f.total_frames = 44100u * 100u;
    snprintf(f.tags.title, sizeof f.tags.title, "%s", title);
    return f;
}
static void start_album(uint32_t index) {
    g_play_ok = true;
    player_play_folder("/Album", g_dir, 4, index);
    player_file_t f = file("t");
    player_file_started(&f); // the storage task's acknowledgement
}
static uint16_t pos(void) {
    player_info_t i;
    player_get(&i);
    return i.queue_pos;
}

TEST(late_announcement_after_a_skip_is_ignored_and_gapless_continues) {
    start_album(0);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path)); // the storage task is offered track 2 ...
    ASSERT_EQ(0, strcmp(path, "/Album/02.flac"));
    player_next();                                     // ... the user skips before it answers
    ASSERT_EQ(2, pos());
    player_file_t f = file("late");
    player_next_announced(&f);                          // the late answer belongs to the old offer
    player_file_t g = file("t2");
    player_file_started(&g);                            // track 2 is up
    // gapless must still be offered for what follows track 2
    ASSERT_TRUE(player_next_track(path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Album/03.flac"));
    player_file_t h = file("third");
    player_next_announced(&h);
    g_changes++;                                        // the audio core hands over
    ASSERT_EQ(3, pos());
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "third"));
}

TEST(handoff_counted_during_the_ramp_down_of_a_skip_does_not_advance_the_queue) {
    start_album(0);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    player_file_t f = file("second");
    player_next_announced(&f);
    player_next();                  // user skips to track 2: hard start requested (state: opening)
    g_changes++;                    // the old stream still crossed its boundary while ramping down
    ASSERT_EQ(2, pos());
    player_file_t g = file("t2");
    player_file_started(&g);        // acknowledged: the counter is re-baselined here
    ASSERT_EQ(2, pos());            // not 3
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "t2"));
    ASSERT_EQ(2, pos());
}

TEST(failed_start_request_rolls_the_queue_position_back) {
    start_album(1);                 // track 2 of 4
    ASSERT_EQ(2, pos());
    g_play_ok = false;
    player_next();                  // "Storage busy"
    ASSERT_EQ(2, pos());            // still on track 2: the next press must not skip track 3
    g_play_ok = true;
    player_next();
    ASSERT_EQ(3, pos());
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/03.flac"));
    g_play_ok = false;
    player_prev();
    ASSERT_EQ(3, pos());
}

TEST(setting_off_offers_nothing) {
    start_album(0);
    g_gapless_setting = 0;
    char path[300];
    ASSERT_FALSE(player_next_track(path, sizeof path));
    g_gapless_setting = 1;
    ASSERT_TRUE(player_next_track(path, sizeof path));
}

static const char *cur_name(void) {
    static player_info_t i;
    player_get(&i);
    return i.name;
}

TEST(repeat_one_replays_the_track_at_its_end_and_offers_it_gaplessly) {
    start_album(1);
    player_set_repeat(PQ_REPEAT_ONE);
    ASSERT_EQ(PQ_REPEAT_ONE, player_repeat());
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Album/02.flac")); // the same file is announced to the audio core
    player_file_t f = file("again");
    player_next_announced(&f);
    g_changes++;                                  // handoff
    ASSERT_EQ(2, pos());
    player_file_started(&f);
    g_ended++;                                    // the track ended with nothing following in the audio core (gapless off, say)
    int calls = g_play_calls;
    player_service();
    ASSERT_EQ(calls + 1, g_play_calls);
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/02.flac"));
    ASSERT_EQ(2, pos());
    player_next();                                // the button still moves on
    ASSERT_EQ(3, pos());
    player_set_repeat(PQ_REPEAT_OFF);
}

TEST(repeat_all_wraps_instead_of_ending) {
    start_album(3);                               // 4 of 4
    player_set_repeat(PQ_REPEAT_ALL);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Album/01.flac")); // gapless look-ahead wraps
    player_file_t f = file("one");
    player_next_announced(&f);
    g_changes++;
    ASSERT_EQ(1, pos());
    player_file_started(&f);
    player_set_repeat(PQ_REPEAT_OFF);
    start_album(3);
    player_set_repeat(PQ_REPEAT_ALL);
    g_ended++;                                    // hard end of the last track
    player_service();
    ASSERT_EQ(1, pos());
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/01.flac"));
    player_set_repeat(PQ_REPEAT_OFF);
    start_album(3);
    player_next();                                // without repeat: the end stops playback
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_IDLE, i.state);
}

TEST(shuffle_toggled_mid_play_keeps_the_track_and_the_handoff_already_announced) {
    start_album(1);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    ASSERT_EQ(0, strcmp(path, "/Album/03.flac"));
    player_file_t f = file("third");
    player_next_announced(&f);
    player_set_shuffle(PQ_SHUFFLE_SONGS);         // the audio core is already going to 03
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(1, i.shuffle);
    ASSERT_EQ(0, strcmp(i.name, "02.flac"));      // still the same track
    ASSERT_EQ(1, i.queue_pos);
    g_changes++;
    ASSERT_EQ(0, strcmp(cur_name(), "03.flac"));  // the handoff the audio core made is what is shown
    ASSERT_EQ(PQ_SHUFFLE_SONGS, player_shuffle());
    player_set_shuffle(PQ_SHUFFLE_OFF);
    ASSERT_EQ(0, strcmp(cur_name(), "03.flac"));
    ASSERT_EQ(3, pos());                          // folder order again, same track
}

TEST(failed_start_after_a_shuffled_wrap_restores_the_whole_queue) {
    g_play_ok = true;
    player_set_repeat(PQ_REPEAT_ALL);
    player_set_shuffle(PQ_SHUFFLE_SONGS);
    player_play_folder("/Album", g_dir, 4, 2);
    player_file_t f = file("t");
    player_file_started(&f);
    char slot3[300];
    for (int i = 0; i < 3; i++) { // to the last slot
        snprintf(slot3, sizeof slot3, "%s", g_last_path); // ends as the path of slot 3 (index 2 of the walk)
        player_next();
        player_file_started(&f);
    }
    ASSERT_EQ(4, pos());
    player_info_t b;
    player_get(&b);
    g_play_ok = false;
    player_next();                  // wraps and re-shuffles, then the start request fails
    player_info_t a;
    player_get(&a);
    ASSERT_EQ(4, pos());
    (void)a; (void)b;
    g_play_ok = true;
    player_prev();                  // no elapsed time known after the failure: goes back one slot, which must be the old slot 3
    ASSERT_EQ(0, strcmp(g_last_path, slot3));
    player_set_repeat(PQ_REPEAT_OFF);
    player_set_shuffle(PQ_SHUFFLE_OFF);
}

int main(void) {
    player_init();
    RUN(late_announcement_after_a_skip_is_ignored_and_gapless_continues);
    RUN(handoff_counted_during_the_ramp_down_of_a_skip_does_not_advance_the_queue);
    RUN(failed_start_request_rolls_the_queue_position_back);
    RUN(setting_off_offers_nothing);
    RUN(repeat_one_replays_the_track_at_its_end_and_offers_it_gaplessly);
    RUN(repeat_all_wraps_instead_of_ending);
    RUN(failed_start_after_a_shuffled_wrap_restores_the_whole_queue);
    RUN(shuffle_toggled_mid_play_keeps_the_track_and_the_handoff_already_announced);
    return RESULT();
}
