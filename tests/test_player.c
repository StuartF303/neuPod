// SPDX-License-Identifier: MPL-2.0
// Player (app/runtime/player.c) against fakes for the audio core, the storage task and the settings store: the queue position and the
// gapless announcement bookkeeping around skips, hard starts and handoffs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"
#include "player.h"
#include "resume.h"
#include "settings_store.h"
#include "unit.h"

// ---- fakes ------------------------------------------------------------------------------------------------------------
static uint32_t g_changes, g_ended;
static bool g_play_ok = true;
static int g_play_calls;
static char g_last_path[300];
static uint8_t g_gapless_setting = 1;
static uint8_t g_set_vol_path = 0xFF; // 0xFF = tag absent
static uint8_t g_set_vol_att = 0xFF;
static uint32_t g_audio_vol = 999, g_pcm_frames = 44100u * 10u;
static int g_pause_calls;
static bool g_paused;
uint32_t audio_track_change_count(void) { return g_changes; }
uint32_t audio_pcm_ended_count(void) { return g_ended; }
bool audio_stats_get(audio_stats_t *o) {
    memset(o, 0, sizeof *o);
    o->source = AUDIO_SRC_PCM;
    o->pcm_frames = g_pcm_frames;
    return true;
}
bool audio_set_volume(uint32_t v) { g_audio_vol = v; return true; }
bool audio_pause(bool p) { g_pause_calls++; g_paused = p; return true; }
bool audio_stop(void) { return true; }
bool audio_tone(uint32_t m) { (void)m; return true; }
bool storage_play_request(const char *path) {
    g_play_calls++;
    snprintf(g_last_path, sizeof g_last_path, "%s", path);
    return g_play_ok;
}
static uint32_t g_last_frame;
bool storage_play_request_at(const char *path, uint32_t frame) {
    g_last_frame = frame;
    return storage_play_request(path);
}
bool storage_verify_request(const char *p) { (void)p; return true; }
bool storage_stop_request(void) { return true; }
bool settings_get_u8(uint32_t tag, uint8_t *out) {
    if (tag == SETTING_VOLUME_PATH) {
        *out = g_set_vol_path;
        return g_set_vol_path != 0xFF;
    }
    *out = g_gapless_setting;
    return true;
}
void settings_set_u8(uint32_t tag, uint8_t v) {
    if (tag == SETTING_VOLUME_PATH) {
        g_set_vol_path = v;
    } else if (tag == SETTING_VOLUME_ATT_DB) {
        g_set_vol_att = v;
    } else {
        g_gapless_setting = v;
    }
}
// resume.c is tested on its own (test_resume.c); here only what the player hands to it
static resume_state_t g_rs_last;
static int g_rs_saves, g_rs_periodic, g_rs_clears;
static bool g_rs_due = true;
bool resume_periodic_due(uint32_t now) { (void)now; return g_rs_due; }
bool resume_save(const resume_state_t *s, resume_why_t why, uint32_t now) {
    (void)now;
    g_rs_last = *s;
    g_rs_saves++;
    g_rs_periodic += why == RESUME_PERIODIC;
    return true;
}
void resume_clear(void) { g_rs_clears++; }
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

TEST(volume_policy_cases) {
    const uint32_t quiet = 40, loud = 10, dflt = PLAYER_RESTORE_MIN_ATT_DB;
    // same known path: the saved level comes back, loud or quiet
    ASSERT_EQ(loud, player_volume_restore_att(loud, OUT_PATH_WIRED, OUT_PATH_WIRED, false));
    ASSERT_EQ(quiet, player_volume_restore_att(quiet, OUT_PATH_BT, OUT_PATH_BT, false));
    ASSERT_EQ(loud, player_volume_restore_att(loud, OUT_PATH_LINE, OUT_PATH_LINE, false));
    // a different path caps at -30 dB (every pair), but never makes a quiet level louder or moves it
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_BT, OUT_PATH_WIRED, false));
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_WIRED, OUT_PATH_BT, false));
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_LINE, OUT_PATH_OPTICAL, false));
    ASSERT_EQ(quiet, player_volume_restore_att(quiet, OUT_PATH_BT, OUT_PATH_WIRED, false));
    ASSERT_EQ(dflt, player_volume_restore_att(dflt, OUT_PATH_BT, OUT_PATH_WIRED, false));
    // headphones detected at boot: only a level saved for line-out is capped
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_LINE, OUT_PATH_LINE, true));
    ASSERT_EQ(loud, player_volume_restore_att(loud, OUT_PATH_WIRED, OUT_PATH_WIRED, true));
    ASSERT_EQ(quiet, player_volume_restore_att(quiet, OUT_PATH_LINE, OUT_PATH_LINE, true));
    // an unknown path on either side (nothing detects it yet, or a volume saved before paths were recorded) is capped
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_UNKNOWN, OUT_PATH_WIRED, false));
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_WIRED, OUT_PATH_UNKNOWN, false));
    ASSERT_EQ(dflt, player_volume_restore_att(loud, OUT_PATH_UNKNOWN, OUT_PATH_UNKNOWN, false));
}

TEST(restore_reads_the_saved_path_and_the_step_writes_it) {
    player_set_output_path(OUT_PATH_UNKNOWN);
    g_set_vol_path = 0xFF;
    player_restore_volume(10); // no path saved: capped
    ASSERT_EQ(PLAYER_RESTORE_MIN_ATT_DB, g_audio_vol);
    // a source reports the path; the volume the user sets is saved together with it
    player_set_output_path(OUT_PATH_BT);
    (void)player_volume_step(0);
    ASSERT_EQ(OUT_PATH_BT, g_set_vol_path);
}

TEST(resume_saves_on_pause_track_change_stop_and_end) {
    int saves = g_rs_saves;
    start_album(1);                               // a new track: saved from its beginning
    ASSERT_TRUE(g_rs_saves > saves);
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Album/02.flac"));
    ASSERT_EQ(1, g_rs_last.index);
    ASSERT_EQ(0, g_rs_last.frame);
    saves = g_rs_saves;
    g_pcm_frames = 44100u * 25u;
    player_pause_toggle();                        // pause: the position is stored
    ASSERT_EQ(saves + 1, g_rs_saves);
    ASSERT_EQ(44100u * 25u, g_rs_last.frame);
    ASSERT_FALSE(g_rs_last.playing);
    player_pause_toggle();
    ASSERT_TRUE(g_rs_last.playing);
    player_next();                                // track change
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Album/03.flac"));
    ASSERT_EQ(0, g_rs_last.frame);
    player_file_t f = file("t");
    player_file_started(&f);
    g_pcm_frames = 44100u * 7u;
    saves = g_rs_saves;
    player_stop();                                // stop keeps the place
    ASSERT_EQ(saves + 1, g_rs_saves);
    ASSERT_EQ(44100u * 7u, g_rs_last.frame);
    ASSERT_FALSE(g_rs_last.playing);
    // the end of the album forgets it
    start_album(3);
    int clears = g_rs_clears;
    player_next();
    ASSERT_EQ(clears + 1, g_rs_clears);
    g_pcm_frames = 44100u * 10u;
}

TEST(periodic_save_only_while_playing_and_only_when_due) {
    start_album(0);
    int periodic = g_rs_periodic;
    g_rs_due = false;
    for (int i = 0; i < 100; i++) player_service();
    ASSERT_EQ(periodic, g_rs_periodic);
    g_rs_due = true;
    for (int i = 0; i < 40; i++) player_service(); // the fake clock runs 1 s per call: the player's own 30 s guard opens
    ASSERT_TRUE(g_rs_periodic > periodic);
    ASSERT_TRUE(g_rs_last.playing);
    periodic = g_rs_periodic;
    player_pause_toggle();
    for (int i = 0; i < 60; i++) player_service(); // paused: no periodic record
    ASSERT_EQ(periodic, g_rs_periodic);
}

TEST(resume_folder_starts_paused_at_the_saved_frame_and_elapsed_includes_it) {
    player_stop();
    g_pause_calls = 0;
    int saves = g_rs_saves;
    ASSERT_TRUE(player_resume_folder("/Album", g_dir, 4, 2, 44100u * 90u));
    ASSERT_EQ(44100u * 90u, g_last_frame);
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/03.flac"));
    ASSERT_EQ(saves, g_rs_saves);                 // opening the resumed track must not overwrite the saved position
    ASSERT_EQ(3, pos());
    player_file_t f = file("resumed");
    f.start_frame = 44100u * 90u;
    player_file_started(&f);
    ASSERT_EQ(1, g_pause_calls);
    ASSERT_TRUE(g_paused);
    g_pcm_frames = 0;                             // the audio core counts from the start of the stream
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);
    ASSERT_EQ(90, i.elapsed_s);
    player_pause_toggle();                        // play: the position is saved from the resume point
    ASSERT_EQ(44100u * 90u, g_rs_last.frame);
    g_pcm_frames = 44100u * 5u;
    player_get(&i);
    ASSERT_EQ(95, i.elapsed_s);
    g_pcm_frames = 44100u * 10u;
    // refused when something already plays
    ASSERT_FALSE(player_resume_folder("/Album", g_dir, 4, 0, 0));
}

TEST(a_resume_that_cannot_start_is_dropped_quietly) {
    player_stop();
    int clears = g_rs_clears;
    ASSERT_TRUE(player_resume_folder("/Album", g_dir, 4, 0, 1000));
    player_file_failed("Cannot open file");
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_IDLE, i.state);
    ASSERT_EQ(clears + 1, g_rs_clears);
}

int main(void) {
    player_init();
    RUN(volume_policy_cases);
    RUN(restore_reads_the_saved_path_and_the_step_writes_it);
    RUN(resume_saves_on_pause_track_change_stop_and_end);
    RUN(periodic_save_only_while_playing_and_only_when_due);
    RUN(resume_folder_starts_paused_at_the_saved_frame_and_elapsed_includes_it);
    RUN(a_resume_that_cannot_start_is_dropped_quietly);
    RUN(late_announcement_after_a_skip_is_ignored_and_gapless_continues);
    RUN(handoff_counted_during_the_ramp_down_of_a_skip_does_not_advance_the_queue);
    RUN(failed_start_request_rolls_the_queue_position_back);
    RUN(setting_off_offers_nothing);
    return RESULT();
}
