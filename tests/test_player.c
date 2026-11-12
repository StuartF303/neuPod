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
static uint8_t g_gapless_setting = 1, g_shuffle_setting, g_repeat_setting, g_resume_play_setting;
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
static uint32_t g_last_frame;
bool storage_play_request(const char *path) {
    g_last_frame = 0; // a plain start begins at the beginning
    g_play_calls++;
    snprintf(g_last_path, sizeof g_last_path, "%s", path);
    return g_play_ok;
}
bool storage_play_request_at(const char *path, uint32_t frame) {
    bool ok = storage_play_request(path);
    g_last_frame = frame;
    return ok;
}
bool storage_verify_request(const char *p) { (void)p; return true; }
bool storage_stop_request(void) { return true; }
static uint8_t *slot(uint32_t tag) { return tag == SETTING_SHUFFLE ? &g_shuffle_setting : tag == SETTING_REPEAT ? &g_repeat_setting : tag == SETTING_RESUME_PLAY ? &g_resume_play_setting : &g_gapless_setting; }
bool settings_get_u8(uint32_t tag, uint8_t *out) {
    if (tag == SETTING_VOLUME_PATH) {
        *out = g_set_vol_path;
        return g_set_vol_path != 0xFF;
    }
    *out = *slot(tag);
    return true;
}
void settings_set_u8(uint32_t tag, uint8_t v) {
    if (tag == SETTING_VOLUME_PATH) {
        g_set_vol_path = v;
    } else if (tag == SETTING_VOLUME_ATT_DB) {
        g_set_vol_att = v;
    } else {
        *slot(tag) = v;
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

// The player counts the start requests it has posted and takes the LAST answer as the one that counts (player_file_started / _failed: the
// answers to replaced starts are dropped). The real storage task answers every request exactly once; a test answers the ones it cares about and
// every test must end with none outstanding: RUN_T checks it, and a test that deliberately leaves starts unanswered calls settle() at its end.
static void settle(void) {
    player_tone_toggle(TONE_OFF);
    for (int guard = 0; player_starts_outstanding() > 0 && guard < 16; guard++) { // (bounded: a broken count must fail a test, not hang it)
        player_file_failed("settle"); // with nothing playing this changes nothing but the count
    }
}

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
    settle();                                            // this test leaves starts unanswered on purpose
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

TEST(player_pause_pauses_a_playing_track_once_and_keeps_the_position) {
    start_album(0);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    g_pcm_frames = 44100u * 12u;
    int calls = g_pause_calls;
    player_pause(); // the headset went away (audio_route.c)
    ASSERT_EQ(calls + 1, g_pause_calls);
    ASSERT_TRUE(g_paused);
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);
    ASSERT_EQ(12u, i.elapsed_s);
    ASSERT_EQ(44100u * 12u, g_rs_last.frame); // the place is kept for the resume record
    player_pause(); // already paused: nothing
    ASSERT_EQ(calls + 1, g_pause_calls);
    player_pause_toggle(); // and Play carries on
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
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

#if RESUME_WRITE_WHILE_PLAYING
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

#endif

TEST(resume_folder_starts_paused_at_the_saved_frame_and_elapsed_includes_it) {
    player_stop();
    g_pause_calls = 0;
    int saves = g_rs_saves;
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 2, 44100u * 90u, false));
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
    ASSERT_EQ(PLAYER_RESUME_BUSY, player_resume_folder("/Album", g_dir, 4, 0, 0, false));
}

TEST(shuffled_player_saves_the_folder_entry_and_resumes_into_a_shuffled_queue) {
    player_stop();
    player_set_shuffle(PQ_SHUFFLE_SONGS);
    start_album(2);                               // entry 2 chosen: slot 0 of the shuffled queue
    player_next();                                // some other entry, whatever the shuffle picked
    player_file_t f = file("t");
    player_file_started(&f);
    g_pcm_frames = 44100u * 3u;
    player_pause_toggle();
    char name[8];
    snprintf(name, sizeof name, "%s", strrchr(g_rs_last.path, '/') + 1);
    unsigned entry = (unsigned)(name[1] - '1');   // "0N.flac" -> N-1: the folder index of the file
    ASSERT_EQ(entry, g_rs_last.index);            // the entry, not the slot
    ASSERT_EQ(44100u * 3u, g_rs_last.frame);
    player_stop();
    // restore: the saved entry is the current track, in a queue that follows the saved shuffle mode
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, g_rs_last.index, g_rs_last.frame, false));
    ASSERT_EQ(0, strcmp(g_last_path + strlen("/Album/"), name));
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(1, i.shuffle);
    ASSERT_EQ(1, i.queue_pos);                    // slot 0 of the new shuffled round
    ASSERT_EQ(4, i.queue_len);
    player_file_started(&f);
    ASSERT_TRUE(g_paused);
    player_set_shuffle(PQ_SHUFFLE_OFF);
    player_stop();
}

TEST(repeat_all_never_clears_the_record_at_the_album_end) {
    player_stop();
    player_set_repeat(PQ_REPEAT_ALL);
    start_album(3);
    int clears = g_rs_clears;
    player_next();                                // wraps instead of ending
    ASSERT_EQ(clears, g_rs_clears);
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Album/01.flac"));
    player_set_repeat(PQ_REPEAT_OFF);
    player_stop();
    settle();                                            // this test leaves starts unanswered on purpose
}

TEST(resume_autoplay_setting_defaults_off_and_round_trips) {
    ASSERT_TRUE(!player_resume_autoplay());
    player_set_resume_autoplay(true);
    ASSERT_TRUE(player_resume_autoplay());
    player_set_resume_autoplay(false);
    ASSERT_TRUE(!player_resume_autoplay());
}

// The caller (storage_service resume_boot) passes play = setting && record.playing. All four combinations, through the same decision.
TEST(boot_resume_plays_only_with_the_setting_on_and_a_record_that_was_playing) {
    for (int setting = 0; setting < 2; setting++) {
        for (int rec_playing = 0; rec_playing < 2; rec_playing++) {
            player_stop();
            player_set_resume_autoplay(setting != 0);
            g_pause_calls = 0;
            g_paused = false;
            bool play = player_resume_autoplay() && rec_playing != 0;
            ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 1, 44100u * 20u, play));
            player_file_t f = file("r");
            f.start_frame = 44100u * 20u;
            player_file_started(&f);
            player_info_t i;
            player_get(&i);
            if (setting && rec_playing) {
                ASSERT_EQ(PLAYER_PLAYING, i.state);
                ASSERT_EQ(0, g_pause_calls);
                ASSERT_TRUE(!g_paused);
            } else {
                ASSERT_EQ(PLAYER_PAUSED, i.state);
                ASSERT_EQ(1, g_pause_calls);
                ASSERT_TRUE(g_paused);
            }
        }
    }
    player_set_resume_autoplay(false);
    player_stop();
}

// A boot resume that is replaced or cancelled while still opening must not make a later failure "quiet": that failure is an ordinary error.
static void expect_ordinary_error_after_failure(int replaced_starts) {
    int clears = g_rs_clears;
    for (int k = 0; k < replaced_starts; k++) {
        player_file_failed("old start"); // the storage task answers the start that was replaced first
    }
    player_file_failed("Cannot open file");
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_ERROR, i.state);
    ASSERT_EQ(clears, g_rs_clears);
}

TEST(a_replaced_autoplay_resume_leaves_no_quiet_failure_behind) {
    player_stop();
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, true));
    player_next();
    expect_ordinary_error_after_failure(1);

    player_stop();
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, true));
    player_play_folder("/Album", g_dir, 4, 1);
    expect_ordinary_error_after_failure(1);

    player_stop();
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, true));
    player_stop();
    start_album(0);                               // a new ordinary start after the Stop
    expect_ordinary_error_after_failure(0);
    player_stop();
}

TEST(a_failure_after_an_autoplay_resume_started_is_an_ordinary_error) {
    player_stop();
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, true));
    player_file_t f = file("r");
    player_file_started(&f);
    expect_ordinary_error_after_failure(0);
    player_stop();
}

TEST(an_autoplay_resume_that_cannot_start_is_still_dropped_quietly) {
    player_stop();
    int clears = g_rs_clears;
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, true));
    player_file_failed("Cannot open file");
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_IDLE, i.state);
    ASSERT_TRUE(g_rs_clears > clears);
}

TEST(a_resume_that_cannot_start_is_dropped_quietly) {
    player_stop();
    int clears = g_rs_clears;
    ASSERT_EQ(PLAYER_RESUME_OK, player_resume_folder("/Album", g_dir, 4, 0, 1000, false));
    player_file_failed("Cannot open file");
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_IDLE, i.state);
    ASSERT_EQ(clears + 1, g_rs_clears);
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
    settle();                                            // this test leaves starts unanswered on purpose
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
    settle();                                            // this test leaves starts unanswered on purpose
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
    settle();                                            // this test leaves starts unanswered on purpose
}

TEST(an_unplayable_last_track_wraps_under_repeat_all_and_an_all_broken_queue_ends) {
    start_album(2);
    player_set_repeat(PQ_REPEAT_ALL);
    g_ended++;
    player_service();                        // auto-advance to track 4 (last)
    ASSERT_EQ(4, pos());
    player_file_failed("cannot open");
    player_service();                        // skipped: wraps to track 1 instead of ending
    ASSERT_EQ(1, pos());
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/01.flac"));
    player_file_t f = file("t");
    player_file_started(&f);                 // a successful start resets the skip count
    // everything broken: one lap at most, then the error stays
    int calls = g_play_calls;
    for (int i = 0; i < 12; i++) {
        player_file_failed("cannot open");
        player_service();
    }
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_ERROR, i.state);
    ASSERT_TRUE(g_play_calls - calls <= 4);
    player_set_repeat(PQ_REPEAT_OFF);
    start_album(2);                          // without repeat-all the last track's failure still ends there
    g_ended++;
    player_service();
    player_file_failed("cannot open");
    player_service();
    player_get(&i);
    ASSERT_EQ(PLAYER_ERROR, i.state);
    ASSERT_EQ(4, pos());
}

// ---- seek (player_seek) --------------------------------------------------------------------------------------------------

static player_file_t seekable_file(const char *title, uint64_t start) {
    player_file_t f = file(title);
    f.seekable = true;
    f.start_frame = start;
    return f;
}
static void start_seekable_album(uint32_t index) {
    g_play_ok = true;
    g_pcm_frames = 44100u * 10u; // the fake audio core has played 10 s of the stream
    player_play_folder("/Album", g_dir, 4, index);
    player_file_t f = seekable_file("t", 0);
    player_file_started(&f);
}

TEST(seek_restarts_the_same_file_at_the_frame_and_the_queue_stays_put) {
    start_seekable_album(1);
    player_info_t i;
    player_get(&i);
    ASSERT_TRUE(i.seekable);
    ASSERT_EQ(44100u * 10u, i.pos_frames);              // the fake audio core has played 10 s
    int calls = g_play_calls;
    ASSERT_TRUE(player_seek(44100u * 30u));
    ASSERT_EQ(calls + 1, g_play_calls);
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/02.flac")); // the same file ...
    ASSERT_EQ(44100u * 30u, g_last_frame);               // ... at the target
    ASSERT_EQ(2, pos());
    player_get(&i);                                      // until the restart is up the screen sees the target
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(44100u * 30u, i.pos_frames);
    ASSERT_EQ(30, i.elapsed_s);
    ASSERT_TRUE(i.seekable);
    g_pcm_frames = 0;                                    // the new stream starts counting from 0
    player_file_t f = seekable_file("t", 44100u * 30u);
    player_file_started(&f);
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(30, i.elapsed_s);
    ASSERT_EQ(44100u * 30u, i.pos_frames);
    ASSERT_EQ(2, pos());
    g_pcm_frames = 44100u * 10u;
}

TEST(seek_on_a_track_that_cannot_seek_does_nothing) {
    start_album(0);                                      // file() is not seekable (MP3 / ALAC / FLAC without a table)
    player_info_t i;
    player_get(&i);
    ASSERT_FALSE(i.seekable);
    int calls = g_play_calls;
    ASSERT_FALSE(player_seek(44100u * 30u));
    ASSERT_EQ(calls, g_play_calls);
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    player_stop();
    ASSERT_FALSE(player_seek(1000));                     // nothing playing at all
}

TEST(seek_while_paused_stays_paused) {
    start_seekable_album(0);
    player_pause_toggle();
    ASSERT_TRUE(g_paused);
    ASSERT_TRUE(player_seek(44100u * 5u));
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);                   // the screen never flashes "Playing"
    g_paused = false;
    player_file_t f = seekable_file("t", 44100u * 5u);
    player_file_started(&f);
    ASSERT_TRUE(g_paused);                               // the restarted stream is paused again
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);
}

TEST(play_pause_during_the_restart_of_a_seek_is_applied_when_it_is_up) {
    start_seekable_album(0);
    ASSERT_TRUE(player_seek(44100u * 5u));
    g_paused = false;
    player_pause_toggle();                               // pressed while the storage task is restarting the stream
    player_file_t f = seekable_file("t", 44100u * 5u);
    player_file_started(&f);
    ASSERT_TRUE(g_paused);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);
    player_pause_toggle();
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
}

TEST(seek_voids_an_announced_gapless_successor_and_offers_it_again) {
    start_seekable_album(0);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));   // track 2 is offered ...
    ASSERT_TRUE(player_seek(44100u * 40u));              // ... the user seeks before it answers
    player_file_t late = seekable_file("late", 0);
    player_next_announced(&late);                        // the late answer belongs to the old stream
    g_changes++;                                         // (and a boundary crossed while the old stream ramped down)
    player_file_t f = seekable_file("t", 44100u * 40u);
    player_file_started(&f);
    ASSERT_EQ(1, pos());                                 // still track 1: neither the answer nor the count moved the queue
    ASSERT_TRUE(player_next_track(path, sizeof path));   // gapless continues for the restarted stream
    ASSERT_EQ(0, strcmp(path, "/Album/02.flac"));
    player_file_t n = seekable_file("second", 0);
    player_next_announced(&n);
    g_changes++;
    ASSERT_EQ(2, pos());
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "second"));
    ASSERT_EQ(0, i.pos_frames - 44100u * 10u);           // the new track counts from its own start
}

TEST(seek_to_the_end_is_the_end_of_the_track) {
    start_seekable_album(1);                             // track 2 of 4
    int calls = g_play_calls;
    ASSERT_TRUE(player_seek(44100u * 100u));             // total_frames of file(): exactly the end
    ASSERT_EQ(calls + 1, g_play_calls);
    ASSERT_EQ(3, pos());                                 // the queue moved on, as for a track that played out
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/03.flac"));
    ASSERT_EQ(0, g_last_frame);                          // from its start
    player_file_t f = seekable_file("t", 0);
    player_file_started(&f);
    ASSERT_TRUE(player_seek(44100u * 100u + 12345u));    // past the end: the same
    ASSERT_EQ(4, pos());
    f = seekable_file("t", 0);
    player_file_started(&f);
    ASSERT_TRUE(player_seek(44100u * 500u));             // the last track: the queue ends, Now Playing goes idle
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_IDLE, i.state);
}

TEST(seek_to_the_end_under_repeat_one_restarts_the_track) {
    start_seekable_album(1);
    player_set_repeat(PQ_REPEAT_ONE);
    ASSERT_TRUE(player_seek(44100u * 100u));
    ASSERT_EQ(2, pos());
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/02.flac"));
    ASSERT_EQ(0, g_last_frame);
    player_set_repeat(PQ_REPEAT_OFF);
    settle();                                            // this test leaves starts unanswered on purpose
}

TEST(a_seek_that_cannot_be_requested_leaves_the_track_playing) {
    start_seekable_album(0);
    g_play_ok = false;                                   // "Storage busy": the old stream is untouched
    ASSERT_TRUE(player_seek(44100u * 20u));
    g_play_ok = true;
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);                  // not an error screen
    ASSERT_EQ(44100u * 10u, i.pos_frames);               // the real position again, not the target
    ASSERT_TRUE(i.seekable);
    ASSERT_TRUE(player_seek(44100u * 20u));              // and a later seek works
    settle();                                            // this test leaves starts unanswered on purpose
}

TEST(a_refused_seek_keeps_the_announced_successor) {
    // The restart cannot be requested ("Storage busy"): the old stream plays on with its successor announced, so the handoff must still
    // move the queue (the seek must not have dropped the announcement).
    start_seekable_album(0);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    player_file_t n = seekable_file("second", 0);
    player_next_announced(&n);
    g_play_ok = false;
    ASSERT_TRUE(player_seek(44100u * 20u));
    g_play_ok = true;
    g_changes++;                                         // the audio core hands over to the announced track
    ASSERT_EQ(2, pos());
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "second"));
}

TEST(skipping_during_a_seek_restart_starts_the_next_track_normally) {
    start_seekable_album(0);
    ASSERT_TRUE(player_seek(44100u * 20u));
    player_next();                                       // Next before the restart reported
    ASSERT_EQ(2, pos());
    g_pcm_frames = 0;
    player_file_t s = seekable_file("seeked", 44100u * 20u);
    player_file_started(&s);                             // the storage task answers the seek's start first ...
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_OPENING, i.state);                  // the answer to the replaced seek does nothing: the next start is still on its way
    player_file_t n = seekable_file("t2", 0);
    player_file_started(&n);                             // ... then the next track's
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(0, strcmp(i.tags.title, "t2"));            // not the seek's info
    ASSERT_EQ(0, i.pos_frames);                          // not counted from the seek's start frame
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/02.flac"));
    ASSERT_EQ(0, g_last_frame);
}

TEST(a_second_seek_before_the_first_one_started_applies_the_second_start) {
    start_seekable_album(0);
    ASSERT_TRUE(player_seek(44100u * 20u));
    ASSERT_TRUE(player_seek(44100u * 50u));
    g_pcm_frames = 0;
    player_file_t a = seekable_file("first", 44100u * 20u);
    player_file_started(&a);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(44100u * 50u, i.pos_frames);               // still showing the latest target
    player_file_t b = seekable_file("second", 44100u * 50u);
    player_file_started(&b);
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "second"));
    ASSERT_EQ(44100u * 50u, i.pos_frames);               // counted from the second start, not the first
    ASSERT_EQ(50, i.elapsed_s);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_TRUE(i.seekable);
}

TEST(a_seek_to_the_end_after_a_seek_applies_the_next_tracks_start) {
    start_seekable_album(0);
    ASSERT_TRUE(player_seek(44100u * 20u));
    ASSERT_TRUE(player_seek(44100u * 100u));             // the end of the track: the queue moves on, a second start request
    ASSERT_EQ(2, pos());
    g_pcm_frames = 0;
    player_file_t a = seekable_file("seeked", 44100u * 20u);
    player_file_started(&a);
    player_file_t b = seekable_file("next track", 0);
    player_file_started(&b);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "next track"));
    ASSERT_EQ(0, i.pos_frames);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(2, pos());
}

TEST(skip_after_skip_applies_the_second_start) {
    start_seekable_album(0);
    player_next();
    player_next();
    ASSERT_EQ(3, pos());
    g_pcm_frames = 0;
    player_file_t a = seekable_file("track two", 0);
    player_file_started(&a);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_OPENING, i.state);                  // track 3 is still being opened
    player_file_t b = seekable_file("track three", 0);
    player_file_started(&b);
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(0, strcmp(i.tags.title, "track three"));
}

TEST(a_failed_replaced_start_does_not_fail_the_one_that_replaced_it) {
    start_seekable_album(0);
    player_next();
    player_next();
    player_file_failed("cannot open track 2");           // the first start's answer: superseded
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_OPENING, i.state);
    player_file_t b = seekable_file("track three", 0);
    player_file_started(&b);
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
}

TEST(an_end_the_old_stream_reports_during_the_seek_window_is_not_a_track_end) {
    start_seekable_album(1);
    ASSERT_TRUE(player_seek(44100u * 30u));
    g_ended++;                                           // the old stream finished while it was being replaced
    g_pcm_frames = 0;
    int calls = g_play_calls;
    player_file_t f = seekable_file("t", 44100u * 30u);
    player_file_started(&f);
    player_service();
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(2, pos());                                 // no spurious move to track 3
    ASSERT_EQ(calls, g_play_calls);
}

TEST(a_finished_seek_saves_the_new_position_for_resume) {
    start_seekable_album(1);
    player_pause_toggle();                               // paused
    int saves = g_rs_saves;
    ASSERT_TRUE(player_seek(44100u * 42u));
    ASSERT_EQ(saves, g_rs_saves);                        // nothing is written for a request: only when the restart is up
    g_pcm_frames = 0;
    player_file_t f = seekable_file("t", 44100u * 42u);
    player_file_started(&f);
    ASSERT_EQ(saves + 1, g_rs_saves);                    // once
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Album/02.flac"));
    ASSERT_EQ(1, g_rs_last.index);
    ASSERT_EQ(44100u * 42u, g_rs_last.frame);
    ASSERT_FALSE(g_rs_last.playing);                     // paused stays paused in the record
    ASSERT_TRUE(player_seek(44100u * 7u));
    player_file_t g = seekable_file("t", 44100u * 7u);
    player_file_started(&g);
    ASSERT_EQ(saves + 2, g_rs_saves);
    ASSERT_EQ(44100u * 7u, g_rs_last.frame);
}

TEST(the_track_id_moves_with_the_track_not_with_a_seek) {
    start_seekable_album(0);
    player_info_t i;
    player_get(&i);
    uint32_t id = i.track_id;
    ASSERT_TRUE(player_seek(44100u * 5u));
    player_file_t f = seekable_file("t", 44100u * 5u);
    player_file_started(&f);
    player_get(&i);
    ASSERT_EQ(id, i.track_id);                           // a seek within the track
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path));
    player_file_t n = seekable_file("second", 0);
    player_next_announced(&n);
    g_changes++;                                         // gapless handoff
    player_get(&i);
    ASSERT_TRUE(i.track_id != id);
    id = i.track_id;
    player_next();                                       // a hard start
    player_file_t m = seekable_file("third", 0);
    player_file_started(&m);
    player_get(&i);
    ASSERT_TRUE(i.track_id != id);
}

TEST(a_mid_stream_error_with_a_start_outstanding_is_replaced_by_that_start) {
    start_seekable_album(0);
    player_next();                                       // a start request is on its way
    ASSERT_EQ(1, player_starts_outstanding());
    player_stream_error("Card read error during playback"); // the old stream broke meanwhile
    ASSERT_EQ(1, player_starts_outstanding());           // not the answer to the start: the count is untouched
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_OPENING, i.state);                  // no error screen for a stream that is being replaced
    player_file_t f = seekable_file("t2", 0);
    player_file_started(&f);                             // the start's own answer still applies
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(0, strcmp(i.tags.title, "t2"));
}

TEST(a_mid_stream_error_with_two_starts_outstanding_does_not_swallow_the_real_answers) {
    start_seekable_album(0);
    player_next();
    player_next();
    ASSERT_EQ(2, player_starts_outstanding());
    player_stream_error("Card read error during playback");
    ASSERT_EQ(2, player_starts_outstanding());
    player_file_t a = seekable_file("track two", 0);
    player_file_started(&a);                             // the replaced start's answer: dropped
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_OPENING, i.state);                  // (before the fix the error had eaten a count and this stale answer was applied)
    player_file_t b = seekable_file("track three", 0);
    player_file_started(&b);
    player_get(&i);
    ASSERT_EQ(PLAYER_PLAYING, i.state);
    ASSERT_EQ(0, strcmp(i.tags.title, "track three"));
}

TEST(a_mid_stream_error_with_nothing_outstanding_is_shown) {
    start_seekable_album(0);
    player_stream_error("Card read error during playback");
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_ERROR, i.state);
    ASSERT_EQ(0, strcmp(i.msg, "Card read error during playback"));
    ASSERT_EQ(0, player_starts_outstanding());
}

TEST(a_refused_seek_behind_an_earlier_seek_leaves_that_seeks_answer_in_charge) {
    start_seekable_album(0);
    player_pause_toggle();                               // paused
    ASSERT_TRUE(player_seek(44100u * 20u));              // seek A is queued
    g_play_ok = false;                                   // the queue is full for seek B
    ASSERT_TRUE(player_seek(44100u * 60u));
    g_play_ok = true;
    ASSERT_EQ(1, player_starts_outstanding());           // only A is outstanding
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);                   // still opening, shown as paused at A's target (not B's)
    ASSERT_EQ(44100u * 20u, i.pos_frames);
    g_pcm_frames = 0;
    g_paused = false;
    player_file_t a = seekable_file("t", 44100u * 20u);
    player_file_started(&a);                             // A's answer finishes the job
    player_get(&i);
    ASSERT_EQ(PLAYER_PAUSED, i.state);
    ASSERT_TRUE(g_paused);
    ASSERT_EQ(44100u * 20u, i.pos_frames);
    ASSERT_EQ(20, i.elapsed_s);
    ASSERT_EQ(0, player_starts_outstanding());
    g_pcm_frames = 44100u * 10u;
}

TEST(a_verify_run_is_not_seekable) {
    player_verify_file("/Album/01.flac", "01.flac");
    player_file_t f = seekable_file("v", 0);
    f.verify = true;
    player_file_started(&f);
    player_info_t i;
    player_get(&i);
    ASSERT_FALSE(i.seekable);
    ASSERT_FALSE(player_seek(1000));
    player_stop();
}


// ---- library queues (player_play_library) --------------------------------------------------------------------------------

// The storage task turns "@lib/<order>/<pos>" into a file; here a made-up path, and a handle it has not resolved is not known.
static char g_known_handle[40];
static bool fake_resolver(const char *handle, char *out, size_t n) {
    if (strcmp(handle, g_known_handle) != 0) {
        return false;
    }
    snprintf(out, n, "/Music/Real/%s.flac", handle + 5);
    return true;
}
static const pq_range_t k_range = {.order = 1, .first = 40, .count = 5};

TEST(a_library_queue_hands_the_storage_task_handles_and_offers_the_next_one_gaplessly) {
    g_play_ok = true;
    player_set_path_resolver(fake_resolver);
    player_play_library(&k_range, 2, false); // the third song of the album is chosen
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/42"));
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(5, i.queue_len);
    ASSERT_EQ(3, i.queue_pos); // "3 of 5"
    player_file_t f = file("Third");
    player_file_started(&f);
    char path[300];
    ASSERT_TRUE(player_next_track(path, sizeof path)); // the gapless look-ahead is a handle too
    ASSERT_EQ(0, strcmp(path, "@lib/1/43"));
    player_file_t g = file("Fourth");
    player_next_announced(&g);
    g_changes++; // the audio core hands over
    ASSERT_EQ(4, pos());
    player_get(&i);
    ASSERT_EQ(0, strcmp(i.tags.title, "Fourth"));
    ASSERT_EQ(5, i.queue_len);
    player_next();
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/44"));
    ASSERT_EQ(5, pos());
    player_file_t h = file("Fifth");
    player_file_started(&h);
    ASSERT_FALSE(player_next_track(path, sizeof path)); // the last song: nothing follows
    settle();
}

TEST(the_resume_record_of_a_library_track_has_its_real_path) {
    g_play_ok = true;
    player_set_path_resolver(fake_resolver);
    snprintf(g_known_handle, sizeof g_known_handle, "@lib/1/41");
    int saves = g_rs_saves;
    player_play_library(&k_range, 1, false);
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/41"));
    ASSERT_EQ(saves + 1, g_rs_saves); // the storage task had resolved it (the resolver knows it): saved from its beginning
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Music/Real/1/41.flac"));
    ASSERT_EQ(0, g_rs_last.index);
    player_file_t f = file("t");
    player_file_started(&f);
    saves = g_rs_saves;
    g_pcm_frames = 44100u * 25u;
    player_pause_toggle(); // pause: the position is stored, with the same real path
    ASSERT_EQ(saves + 1, g_rs_saves);
    ASSERT_EQ(0, strcmp(g_rs_last.path, "/Music/Real/1/41.flac"));
    ASSERT_EQ(44100u * 25u, g_rs_last.frame);
    // a track the storage task has not resolved yet cannot be saved (the record waits for the next save): nothing wrong is saved
    snprintf(g_known_handle, sizeof g_known_handle, "nothing");
    saves = g_rs_saves;
    player_pause_toggle();
    player_next();
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/42"));
    ASSERT_TRUE(g_rs_last.path[0] == '/'); // (whatever was saved is a real path, never a handle)
    g_pcm_frames = 44100u * 10u;
    settle();
}

TEST(shuffle_songs_shuffles_the_whole_range_without_touching_the_setting) {
    g_play_ok = true;
    g_shuffle_setting = 0;
    pq_range_t all = {.order = 0, .first = 0, .count = 200};
    player_play_library(&all, PLAY_QUEUE_NO_ENTRY, true);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(200, i.queue_len);
    ASSERT_EQ(1, i.queue_pos); // the random opener is slot 0
    ASSERT_EQ(1, i.shuffle);
    ASSERT_EQ(0, g_shuffle_setting); // Settings > Shuffle is as it was
    ASSERT_TRUE(g_last_path[0] == '@');
    player_file_t f = file("t");
    player_file_started(&f);
    char a[40], b[40];
    snprintf(a, sizeof a, "%s", g_last_path);
    player_next();
    snprintf(b, sizeof b, "%s", g_last_path);
    ASSERT_TRUE(strcmp(a, b) != 0); // the next song is another one
    ASSERT_EQ(2, pos());
    player_play_folder("/Album", g_dir, 4, 0); // a folder after the library: an ordinary folder queue again
    player_get(&i);
    ASSERT_EQ(4, i.queue_len);
    ASSERT_EQ(0, strcmp(g_last_path, "/Album/01.flac"));
    settle();
}

TEST(a_library_track_that_cannot_be_played_is_skipped_like_a_broken_file) {
    g_play_ok = true;
    player_set_repeat(PQ_REPEAT_OFF);
    player_play_library(&k_range, 0, false);
    player_file_t f = file("first");
    player_file_started(&f);
    g_ended++;
    player_service(); // the first ended: the second is started
    ASSERT_EQ(2, pos());
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/41"));
    player_file_failed("Can't play: unsupported codec (AAC)"); // the storage task refused it
    player_service();
    ASSERT_EQ(3, pos()); // skipped
    ASSERT_EQ(0, strcmp(g_last_path, "@lib/1/42"));
    settle();
}

TEST(an_empty_library_range_starts_nothing) {
    g_play_ok = true;
    int calls = g_play_calls;
    player_play_library(&(pq_range_t){.order = 0, .first = 0, .count = 0}, 0, false);
    player_play_library(&k_range, 9, false); // outside the range
    ASSERT_EQ(calls, g_play_calls);
    player_info_t i;
    player_get(&i);
    ASSERT_EQ(0, i.queue_len);
}


#define RUN_T(name) do { RUN(name); ASSERT_EQ(0, player_starts_outstanding()); } while (0)

int main(void) {
    player_init();
    RUN_T(a_library_queue_hands_the_storage_task_handles_and_offers_the_next_one_gaplessly);
    RUN_T(the_resume_record_of_a_library_track_has_its_real_path);
    RUN_T(shuffle_songs_shuffles_the_whole_range_without_touching_the_setting);
    RUN_T(a_library_track_that_cannot_be_played_is_skipped_like_a_broken_file);
    RUN_T(an_empty_library_range_starts_nothing);
    RUN_T(volume_policy_cases);
    RUN_T(restore_reads_the_saved_path_and_the_step_writes_it);
    RUN_T(player_pause_pauses_a_playing_track_once_and_keeps_the_position);
    RUN_T(resume_saves_on_pause_track_change_stop_and_end);
#if RESUME_WRITE_WHILE_PLAYING
    RUN_T(periodic_save_only_while_playing_and_only_when_due);
#endif
    RUN_T(resume_folder_starts_paused_at_the_saved_frame_and_elapsed_includes_it);
    RUN_T(shuffled_player_saves_the_folder_entry_and_resumes_into_a_shuffled_queue);
    RUN_T(repeat_all_never_clears_the_record_at_the_album_end);
    RUN_T(a_resume_that_cannot_start_is_dropped_quietly);
    RUN_T(resume_autoplay_setting_defaults_off_and_round_trips);
    RUN_T(boot_resume_plays_only_with_the_setting_on_and_a_record_that_was_playing);
    RUN_T(an_autoplay_resume_that_cannot_start_is_still_dropped_quietly);
    RUN_T(a_replaced_autoplay_resume_leaves_no_quiet_failure_behind);
    RUN_T(a_failure_after_an_autoplay_resume_started_is_an_ordinary_error);
    RUN_T(late_announcement_after_a_skip_is_ignored_and_gapless_continues);
    RUN_T(handoff_counted_during_the_ramp_down_of_a_skip_does_not_advance_the_queue);
    RUN_T(failed_start_request_rolls_the_queue_position_back);
    RUN_T(setting_off_offers_nothing);
    RUN_T(repeat_one_replays_the_track_at_its_end_and_offers_it_gaplessly);
    RUN_T(repeat_all_wraps_instead_of_ending);
    RUN_T(failed_start_after_a_shuffled_wrap_restores_the_whole_queue);
    RUN_T(an_unplayable_last_track_wraps_under_repeat_all_and_an_all_broken_queue_ends);
    RUN_T(shuffle_toggled_mid_play_keeps_the_track_and_the_handoff_already_announced);
    RUN_T(seek_restarts_the_same_file_at_the_frame_and_the_queue_stays_put);
    RUN_T(seek_on_a_track_that_cannot_seek_does_nothing);
    RUN_T(seek_while_paused_stays_paused);
    RUN_T(play_pause_during_the_restart_of_a_seek_is_applied_when_it_is_up);
    RUN_T(seek_voids_an_announced_gapless_successor_and_offers_it_again);
    RUN_T(seek_to_the_end_is_the_end_of_the_track);
    RUN_T(seek_to_the_end_under_repeat_one_restarts_the_track);
    RUN_T(a_seek_that_cannot_be_requested_leaves_the_track_playing);
    RUN_T(a_refused_seek_keeps_the_announced_successor);
    RUN_T(skipping_during_a_seek_restart_starts_the_next_track_normally);
    RUN_T(a_second_seek_before_the_first_one_started_applies_the_second_start);
    RUN_T(a_seek_to_the_end_after_a_seek_applies_the_next_tracks_start);
    RUN_T(skip_after_skip_applies_the_second_start);
    RUN_T(a_failed_replaced_start_does_not_fail_the_one_that_replaced_it);
    RUN_T(an_end_the_old_stream_reports_during_the_seek_window_is_not_a_track_end);
    RUN_T(a_finished_seek_saves_the_new_position_for_resume);
    RUN_T(the_track_id_moves_with_the_track_not_with_a_seek);
    RUN_T(a_mid_stream_error_with_a_start_outstanding_is_replaced_by_that_start);
    RUN_T(a_mid_stream_error_with_two_starts_outstanding_does_not_swallow_the_real_answers);
    RUN_T(a_mid_stream_error_with_nothing_outstanding_is_shown);
    RUN_T(a_refused_seek_behind_an_earlier_seek_leaves_that_seeks_answer_in_charge);
    RUN_T(a_verify_run_is_not_seekable);
    return RESULT();
}
