// SPDX-License-Identifier: MPL-2.0
// The library service (app/runtime/library_service.c: the storage task's half of the Music screens) against the real library on a FatFs disk
// image and fakes for the player, the audio core, the card and the storage queue: the request / answer protocol, the row window, building (and
// when it refuses: something playing, a write-protected card, no card; and when it stops: Cancel, another request waiting), the shared buffer
// being taken over by a folder listing, and the queue-handle resolver with the file path it keeps for the resume record.
#include "library_fixture.h"

#include "hal_host.h"

#include "library.h"
#include "library_service.h"
#include "meta.h"
#include "nfs.h"
#include "../app/library/lib_internal.h" /* lib_exists */
#include "os.h"
#include "player.h"
#include "track_stream.h"
#include "unit.h"

/* ---- fakes ---------------------------------------------------------------------------------------------------------- */

static player_state_t g_pstate = PLAYER_IDLE;
void player_get(player_info_t *o) {
    memset(o, 0, sizeof *o);
    o->state = g_pstate;
}
static int g_player_stops;
void player_stop(void) { /* as the real one: the place is kept for resume, the player goes idle */
    g_player_stops++;
    g_pstate = PLAYER_IDLE;
}
static int g_audio_stops;
bool audio_stop(void) {
    g_audio_stops++;
    return true;
}
bool audio_stats_get(audio_stats_t *o) {
    memset(o, 0, sizeof *o);
    o->source = AUDIO_SRC_NONE;
    return true;
}
static int g_stream_stops, g_pumps;
void track_stream_stop(void) { g_stream_stops++; }
void track_stream_pump(void) { g_pumps++; }
static uint8_t g_arena[99u * 1024u] __attribute__((aligned(8)));
void *codec_arena_borrow(size_t *n) {
    *n = sizeof g_arena;
    return g_arena;
}
struct os_mutex { int x; };
os_mutex_t *os_mutex_create(void) { return calloc(1, sizeof(struct os_mutex)); }
bool os_mutex_lock(os_mutex_t *m, uint32_t t) { (void)m; (void)t; return true; }
void os_mutex_unlock(os_mutex_t *m) { (void)m; }
uint32_t os_tick_ms(void) { static uint32_t t; return t += 10; }
static int g_delays, g_cancel_after_delays;
void os_delay_ms(uint32_t ms) {
    (void)ms;
    g_delays++;
    if (g_cancel_after_delays != 0 && g_delays == g_cancel_after_delays) library_cancel_build(); /* the user presses Cancel during the scan */
}
// the storage queue: a posted request is kept, and the test runs it on "the storage task" (library_service_handle) when it says so
static uint8_t g_blob[128];
static size_t g_blob_n;
static bool g_waiting;
bool storage_post_library(const void *blob, size_t n) {
    memcpy(g_blob, blob, n);
    g_blob_n = n;
    return true;
}
bool storage_request_waiting(void) { return g_waiting; }

static void run_storage_task(void) { library_service_handle(g_blob, g_blob_n); }

static uint32_t g_seq;
static libsvc_status_t do_info(lvw_view_t v) {
    libsvc_status_t s = LIBSVC_ERROR;
    ASSERT_TRUE(library_request_info(v, ++g_seq));
    ASSERT_FALSE(library_poll(LIBSVC_REQ_INFO, g_seq, &s)); /* not answered until the storage task has run it */
    run_storage_task();
    ASSERT_TRUE(library_poll(LIBSVC_REQ_INFO, g_seq, &s));
    return s;
}
static libsvc_status_t do_rows(lvw_view_t v, uint32_t first, uint32_t n) {
    libsvc_status_t s = LIBSVC_ERROR;
    ASSERT_TRUE(library_request_rows(v, first, n, ++g_seq));
    run_storage_task();
    ASSERT_TRUE(library_poll(LIBSVC_REQ_ROWS, g_seq, &s));
    return s;
}
static libsvc_status_t do_build(void) {
    libsvc_status_t s = LIBSVC_ERROR;
    ASSERT_TRUE(library_request_build(++g_seq));
    run_storage_task();
    ASSERT_TRUE(library_poll(LIBSVC_REQ_BUILD, g_seq, &s));
    return s;
}

/* ---- a small tagged tree -------------------------------------------------------------------------------------------- */

static uint8_t g_file[16384];

static bool put_flac(const char *path, const char *title, const char *artist, const char *album, int track) {
    fx_tags_t t = {.title = title, .artist = artist, .album = album, .genre = "Rock", .track = track};
    return fx_put(path, g_file, fx_flac(g_file, &t, 200));
}

static bool make_tree(int albums) {
    bool ok = true;
    for (int a = 0; a < albums; a++) {
        char alb[32], path[96], title[32];
        snprintf(alb, sizeof alb, "Album %02d", a);
        for (int t = 1; t <= 3; t++) {
            snprintf(path, sizeof path, "/Music/Band/%s/%02d.flac", alb, t);
            snprintf(title, sizeof title, "Song %02d-%d", a, t);
            ok &= put_flac(path, title, "Band", alb, t);
        }
    }
    fx_tags_t aac = {.title = "Greyed", .artist = "Other", .album = "Aac", .track = 1};
    ok &= fx_put("/Music/Other/Aac/01.m4a", g_file, fx_m4a_aac(g_file, &aac));
    return ok;
}

static bool fresh_card(int albums) { return fx_volume(16u << 20, 0) && make_tree(albums); }

/* ---- tests ------------------------------------------------------------------------------------------------------------ */

TEST(no_library_says_so_and_a_build_makes_one) {
    ASSERT_TRUE(fresh_card(4));
    ASSERT_EQ(LIBSVC_NONE, do_info((lvw_view_t){LVW_ARTISTS, 0}));
    ASSERT_FALSE(library_known_ready());
    ASSERT_EQ(LIBSVC_OK, do_build());
    library_progress_t p;
    library_progress(&p);
    ASSERT_FALSE(p.active);
    ASSERT_EQ(13, p.tracks);
    ASSERT_EQ(2, p.artists);
    ASSERT_EQ(5, p.albums);
    ASSERT_TRUE(g_audio_stops > 0 && g_stream_stops > 0); /* the audio core was let go of before the arena was used */
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_ARTISTS, 0}));
    ASSERT_TRUE(library_known_ready());
    lvw_info_t in;
    library_info(&in);
    ASSERT_EQ(2, in.count);
    ASSERT_EQ(0, strcmp(in.title, "Artists"));
}

TEST(rows_come_in_a_window_and_only_for_the_list_they_were_filled_for) {
    ASSERT_TRUE(fresh_card(7)); /* 21 songs by Band */
    ASSERT_EQ(LIBSVC_OK, do_build());
    lvw_view_t songs = {LVW_SONGS, 0};
    ASSERT_EQ(LIBSVC_OK, do_info(songs));
    lvw_info_t in;
    library_info(&in);
    ASSERT_EQ(22, in.count);
    lvw_row_t r;
    ASSERT_FALSE(library_row(songs, 0, &r)); /* nothing filled yet */
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 0, LIBSVC_ROWS));
    for (uint32_t i = 0; i < LIBSVC_ROWS; i++) {
        ASSERT_TRUE(library_row(songs, i, &r));
        ASSERT_TRUE(r.text[0] != '\0');
        ASSERT_EQ(LVW_KINDS, r.child.kind);
    }
    ASSERT_FALSE(library_row(songs, LIBSVC_ROWS, &r)); /* outside the window: a placeholder until the next fill */
    ASSERT_FALSE(library_row((lvw_view_t){LVW_ALBUMS, 0}, 0, &r)); /* another list's rows do not match */
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 10, LIBSVC_ROWS)); /* the window moves with the selection */
    ASSERT_TRUE(library_row(songs, 10, &r));
    ASSERT_TRUE(library_row(songs, 21, &r));
    ASSERT_FALSE(library_row(songs, 9, &r));
    ASSERT_FALSE(library_row(songs, 22, &r)); /* past the end of the list */
    /* the window is cut at the end of the list */
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 20, LIBSVC_ROWS));
    ASSERT_TRUE(library_row(songs, 21, &r));
    ASSERT_FALSE(library_row(songs, 22, &r));
    /* a child list: the album's songs, in track order, from a window of their own */
    lvw_view_t arts = {LVW_ARTISTS, 0};
    ASSERT_EQ(LIBSVC_OK, do_info(arts));
    ASSERT_EQ(LIBSVC_OK, do_rows(arts, 0, LIBSVC_ROWS));
    ASSERT_TRUE(library_row(arts, 0, &r));
    ASSERT_EQ(LVW_ARTIST, r.child.kind);
    lvw_view_t band = r.child;
    ASSERT_EQ(LIBSVC_OK, do_info(band));
    ASSERT_EQ(LIBSVC_OK, do_rows(band, 0, LIBSVC_ROWS));
    ASSERT_TRUE(library_row(band, 1, &r));
    ASSERT_EQ(0, strcmp(r.text, "Album 00"));
    ASSERT_FALSE(library_row(arts, 0, &r)); /* the window now belongs to the artist's list */
}

TEST(rows_error_on_a_bad_view_and_the_service_keeps_working) {
    ASSERT_TRUE(fresh_card(2));
    ASSERT_EQ(LIBSVC_OK, do_build());
    ASSERT_EQ(LIBSVC_DAMAGED, do_info((lvw_view_t){LVW_ARTIST, 99})); /* an id the index does not have: the index is not what the list expects */
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
}

TEST(a_failed_list_open_does_not_leave_the_previous_lists_rows_computed_from_its_empty_info) {
    ASSERT_TRUE(fresh_card(2));
    ASSERT_EQ(LIBSVC_OK, do_build());
    lvw_view_t songs = {LVW_SONGS, 0};
    ASSERT_EQ(LIBSVC_OK, do_info(songs));
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 0, LIBSVC_ROWS));
    ASSERT_EQ(LIBSVC_DAMAGED, do_info((lvw_view_t){LVW_ARTIST, 99})); /* a child list that cannot open ... */
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 0, LIBSVC_ROWS));             /* ... and back in the parent: it still fills */
    lvw_row_t r;
    ASSERT_TRUE(library_row(songs, 0, &r));
    ASSERT_TRUE(r.text[0] != '\0');
}

TEST(a_folder_listing_takes_the_shared_buffer_and_the_next_request_opens_the_reader_again) {
    ASSERT_TRUE(fresh_card(3));
    ASSERT_EQ(LIBSVC_OK, do_build());
    lvw_view_t songs = {LVW_SONGS, 0};
    ASSERT_EQ(LIBSVC_OK, do_info(songs));
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 0, LIBSVC_ROWS));
    lvw_row_t r;
    ASSERT_TRUE(library_row(songs, 0, &r));
    file_entry_t *entries = library_service_take_listing(); /* the Files screen lists a folder: it gets the buffer */
    ASSERT_TRUE(entries != NULL);
    memset(entries, 'x', sizeof entries[0] * FILES_MAX_ENTRIES); /* ... and fills all of it */
    ASSERT_FALSE(library_row(songs, 0, &r)); /* the rows are gone, and nobody reads the clobbered memory */
    ASSERT_EQ(LIBSVC_OK, do_info(songs)); /* the next request opens the reader again, from scratch */
    ASSERT_EQ(LIBSVC_OK, do_rows(songs, 0, LIBSVC_ROWS));
    ASSERT_TRUE(library_row(songs, 0, &r));
    ASSERT_TRUE(r.text[0] >= 'A' && r.text[0] <= 'Z');
}

TEST(asking_for_a_build_stops_a_paused_track_but_not_a_playing_one) {
    g_pstate = PLAYER_PAUSED; /* after boot: resume brought the last track back paused */
    g_player_stops = 0;
    ASSERT_TRUE(library_request_build(++g_seq));
    ASSERT_EQ(1, g_player_stops);
    ASSERT_EQ(PLAYER_IDLE, g_pstate);
    g_pstate = PLAYER_PLAYING;
    ASSERT_TRUE(library_request_build(++g_seq));
    ASSERT_EQ(1, g_player_stops); /* something actually playing is left alone: the build then refuses */
    g_pstate = PLAYER_IDLE;
    library_cancel_build();
}

TEST(a_build_refuses_while_something_plays_and_on_a_card_it_cannot_write) {
    ASSERT_TRUE(fresh_card(2));
    g_pstate = PLAYER_PLAYING;
    ASSERT_EQ(LIBSVC_PLAYING, do_build());
    g_pstate = PLAYER_OPENING; /* (a paused track is stopped by the request: asking_for_a_build_stops_a_paused_track...) */
    ASSERT_EQ(LIBSVC_PLAYING, do_build());
    ASSERT_EQ(LIBSVC_NONE, do_info((lvw_view_t){LVW_SONGS, 0})); /* nothing was written */
    g_pstate = PLAYER_ENDED; /* a finished track is nothing playing */
    hal_host_storage_set_read_only(true);
    ASSERT_EQ(LIBSVC_NEEDS_WRITABLE, do_build()); /* said up front, before anything is attempted */
    ASSERT_EQ(LIBSVC_NONE, do_info((lvw_view_t){LVW_SONGS, 0}));
    hal_host_storage_set_read_only(false);
    ASSERT_EQ(LIBSVC_OK, do_build());
    g_pstate = PLAYER_IDLE;
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
    hal_storage_set_write_enabled(false);
    ASSERT_EQ(LIBSVC_OK, do_build()); /* the build takes the write lock itself, for its own duration */
}

TEST(no_card_means_no_library_and_no_build) {
    ASSERT_TRUE(fresh_card(2));
    ASSERT_EQ(LIBSVC_OK, do_build());
    hal_host_storage_set_mounted(false);
    ASSERT_EQ(LIBSVC_NO_CARD, do_info((lvw_view_t){LVW_SONGS, 0}));
    ASSERT_EQ(LIBSVC_NO_CARD, do_build());
    char p[64];
    const char *why = "";
    ASSERT_FALSE(library_service_resolve("@lib/0/0", p, sizeof p, &why));
    ASSERT_EQ(0, strcmp(why, "No card"));
    hal_host_storage_set_mounted(true);
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
}

TEST(cancel_stops_the_scan_and_the_old_library_survives) {
    ASSERT_TRUE(fresh_card(9)); /* 28 files: the scan yields every few files */
    ASSERT_EQ(LIBSVC_OK, do_build());
    lvw_info_t old;
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
    library_info(&old);
    ASSERT_EQ(28, old.count);
    ASSERT_TRUE(put_flac("/Music/Band/New/01.flac", "Brand New", "Band", "New", 1));
    g_delays = 0;
    g_cancel_after_delays = 2; /* Cancel is pressed during the scan */
    ASSERT_EQ(LIBSVC_CANCELLED, do_build());
    g_cancel_after_delays = 0;
    library_progress_t p;
    library_progress(&p);
    ASSERT_FALSE(p.active);
    lvw_info_t now;
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
    library_info(&now);
    ASSERT_EQ(28, now.count); /* the new file is not in it: the old library is untouched */
    ASSERT_FALSE(lib_exists("/.neupod/lib.tmp"));
    /* another request waiting (a Play press) also stops it, but that is not Cancel: BUSY, not CANCELLED */
    g_waiting = true;
    ASSERT_EQ(LIBSVC_BUSY, do_build());
    g_waiting = false;
    ASSERT_EQ(LIBSVC_OK, do_build()); /* and a later build starts clean: the cancel flag was cleared by the request */
    ASSERT_EQ(LIBSVC_OK, do_info((lvw_view_t){LVW_SONGS, 0}));
    library_info(&now);
    ASSERT_EQ(29, now.count);
}

TEST(handles_resolve_to_files_and_unplayable_tracks_say_why) {
    ASSERT_TRUE(fresh_card(2));
    ASSERT_EQ(LIBSVC_OK, do_build());
    char p[256];
    const char *why = "";
    ASSERT_FALSE(library_service_resolve("@lib/0/0", p, sizeof p, &why)); /* all songs by title: "Greyed" (the AAC file) comes first */
    ASSERT_EQ(0, strcmp(why, "Can't play: unsupported codec (AAC)"));
    ASSERT_TRUE(library_service_resolve("@lib/0/1", p, sizeof p, &why));
    ASSERT_TRUE(strncmp(p, "/Music/", 7) == 0);
    /* find the AAC track in the title order and see what the resolver says */
    int unplayable = 0;
    for (uint32_t pos = 0; pos < 7; pos++) {
        char h[40];
        snprintf(h, sizeof h, "@lib/0/%u", (unsigned)pos);
        if (!library_service_resolve(h, p, sizeof p, &why)) {
            unplayable++;
            ASSERT_EQ(0, strcmp(why, "Can't play: unsupported codec (AAC)"));
        }
    }
    ASSERT_EQ(1, unplayable);
    ASSERT_FALSE(library_service_resolve("@lib/0/7", p, sizeof p, &why)); /* past the end */
    ASSERT_TRUE(strstr(why, "Library changed") != NULL);
    ASSERT_FALSE(library_service_resolve("/Music/x.flac", p, sizeof p, &why)); /* not a handle */
    ASSERT_FALSE(library_service_resolve("@lib/9/0", p, sizeof p, &why));     /* not an order */
    ASSERT_FALSE(library_service_resolve("@lib/0/zz", p, sizeof p, &why));
    ASSERT_FALSE(library_service_resolve("@lib/0/1", p, 8, &why)); /* the path does not fit: not truncated into another file */
}

TEST(the_real_path_of_the_last_two_resolved_handles_is_kept_for_the_resume_record) {
    ASSERT_TRUE(fresh_card(2));
    ASSERT_EQ(LIBSVC_OK, do_build());
    char a[256], b[256], c[256], got[256];
    const char *why;
    ASSERT_TRUE(library_service_resolve("@lib/0/1", a, sizeof a, &why));
    ASSERT_TRUE(library_service_resolve("@lib/0/2", b, sizeof b, &why));
    ASSERT_TRUE(library_service_real_path("@lib/0/1", got, sizeof got));
    ASSERT_EQ(0, strcmp(got, a));
    ASSERT_TRUE(library_service_real_path("@lib/0/2", got, sizeof got));
    ASSERT_EQ(0, strcmp(got, b));
    ASSERT_TRUE(library_service_resolve("@lib/0/3", c, sizeof c, &why)); /* the oldest is replaced */
    ASSERT_FALSE(library_service_real_path("@lib/0/1", got, sizeof got));
    ASSERT_TRUE(library_service_real_path("@lib/0/3", got, sizeof got));
    ASSERT_EQ(0, strcmp(got, c));
    ASSERT_FALSE(library_service_real_path("@lib/0/6", got, sizeof got));
    ASSERT_FALSE(library_service_real_path("@lib/0/2", got, 4)); /* too small: nothing copied */
}

TEST(status_texts_are_plain_language) {
    ASSERT_EQ(0, strcmp(library_status_text(LIBSVC_NEEDS_WRITABLE), "Library needs a writable card"));
    ASSERT_EQ(0, strcmp(library_status_text(LIBSVC_PLAYING), "Stop playback to update the library"));
    for (int s = LIBSVC_NO_CARD; s <= LIBSVC_BUSY; s++) ASSERT_TRUE(library_status_text((libsvc_status_t)s)[0] != '\0');
}

int main(void) {
    library_service_init();
    RUN(no_library_says_so_and_a_build_makes_one);
    RUN(rows_come_in_a_window_and_only_for_the_list_they_were_filled_for);
    RUN(rows_error_on_a_bad_view_and_the_service_keeps_working);
    RUN(a_failed_list_open_does_not_leave_the_previous_lists_rows_computed_from_its_empty_info);
    RUN(a_folder_listing_takes_the_shared_buffer_and_the_next_request_opens_the_reader_again);
    RUN(asking_for_a_build_stops_a_paused_track_but_not_a_playing_one);
    RUN(a_build_refuses_while_something_plays_and_on_a_card_it_cannot_write);
    RUN(no_card_means_no_library_and_no_build);
    RUN(cancel_stops_the_scan_and_the_old_library_survives);
    RUN(handles_resolve_to_files_and_unplayable_tracks_say_why);
    RUN(the_real_path_of_the_last_two_resolved_handles_is_kept_for_the_resume_record);
    RUN(status_texts_are_plain_language);
    fx_cleanup();
    return RESULT();
}
