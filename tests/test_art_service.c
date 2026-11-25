// SPDX-License-Identifier: MPL-2.0
// art_service against the host file system: a track change that arrives while a decode is in progress (the storage task's yield hook
// feeds the audio stream, and the gapless handoff then retargets the job) must not publish the old picture under the new track.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "art_service.h"
#include "os.h"
#include "unit.h"

uint32_t os_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
void os_delay_ms(uint32_t ms) { usleep(ms * 1000u); }
void os_yield(void) { sched_yield(); }

static char g_root[] = "/tmp/neupod_artsvc_XXXXXX";

static void copy(const char *src, const char *dst) {
    FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
    static char buf[65536];
    size_t n = a ? fread(buf, 1, sizeof buf, a) : 0;
    if (b && n) fwrite(buf, 1, n, b);
    if (a) fclose(a);
    if (b) fclose(b);
}

static audio_tags_t tags(const char *album) {
    audio_tags_t t;
    memset(&t, 0, sizeof t);
    snprintf(t.album, sizeof t.album, "%s", album);
    return t;
}

static int g_calls;
static bool g_retarget_done;
static bool retarget_once(void *ctx) { // what track_stream_pump does through ts_track_changed in the middle of a decode
    (void)ctx;
    if (++g_calls == 3 && !g_retarget_done) {
        g_retarget_done = true;
        audio_tags_t t = tags("Beta");
        art_service_track("/B/t2.flac", &t);
    }
    return true;
}
static bool plain_yield(void *ctx) { (void)ctx; return true; }

TEST(retargeting_during_a_decode_never_shows_the_old_cover_for_the_new_track) {
    char a[600], b[600];
    snprintf(a, sizeof a, "%s/A", g_root);
    snprintf(b, sizeof b, "%s/B", g_root);
    mkdir(a, 0777);
    mkdir(b, 0777);
    char src[600], dst[700];
    snprintf(src, sizeof src, "%s/q420_500x333.jpg", NEUPOD_ART_ASSETS);   // album A: 500x333 -> thumbnail 63..64 wide
    snprintf(dst, sizeof dst, "%s/cover.jpg", a);
    copy(src, dst);
    snprintf(src, sizeof src, "%s/q420_40x30.jpg", NEUPOD_ART_ASSETS);      // album B: 40x30, shown at its own size
    snprintf(dst, sizeof dst, "%s/cover.jpg", b);
    copy(src, dst);

    audio_tags_t ta = tags("Alpha");
    art_service_track("/A/t1.flac", &ta);
    ASSERT_TRUE(art_service_pending());
    art_service_run(retarget_once, NULL);
    ASSERT_TRUE(g_retarget_done);
    // The run for album A was abandoned: nothing is published, the new job is still queued, the view says "loading".
    art_view_t v;
    art_view_get(&v);
    ASSERT_TRUE(art_service_pending());
    ASSERT_EQ(ART_VIEW_LOADING, v.state);
    // The queued job now runs for album B and publishes B's picture.
    art_service_run(plain_yield, NULL);
    art_view_get(&v);
    ASSERT_FALSE(art_service_pending());
    ASSERT_EQ(ART_VIEW_READY, v.state);
    printf("  shown cover is %ux%u (album B's 40x30; album A would be larger)\n", (unsigned)v.w, (unsigned)v.h);
    ASSERT_EQ(40, v.w);
    ASSERT_EQ(30, v.h);
    // A later track of the same album B does not redo anything.
    audio_tags_t tb = tags("Beta");
    art_service_track("/B/t3.flac", &tb);
    ASSERT_FALSE(art_service_pending());
}

#ifdef NEUPOD_ART_HIRES
TEST(an_album_played_before_comes_back_from_the_cache_without_decoding) {
    art_view_t v;
    audio_tags_t ta = tags("Alpha"), tb = tags("Beta");
    art_service_track("/A/t9.flac", &ta); /* album A was abandoned mid-decode earlier: not cached, so it decodes now */
    ASSERT_TRUE(art_service_pending());
    art_service_run(plain_yield, NULL);
    art_view_get(&v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    ASSERT_TRUE(v.w > 40 && v.w <= ART_MAX_DIM); /* album A's 500x333 picture */
    uint16_t wa = v.w;
    art_service_track("/B/t10.flac", &tb); /* B was decoded before: a hit, nothing queued, its own size back */
    ASSERT_FALSE(art_service_pending());
    art_view_get(&v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    ASSERT_EQ(40, v.w);
    art_service_track("/A/t11.flac", &ta); /* ... and A again, from the cache too */
    ASSERT_FALSE(art_service_pending());
    art_view_get(&v);
    ASSERT_EQ(wa, v.w);
    ASSERT_EQ(160, ART_MAX_DIM);
}
#endif

#ifdef NEUPOD_ART_HIRES
// The Cover Flow asks for five albums at once: handles "@lib/1/<n>" stand for folders C<n> (a cover file each; C6 has none).
static bool fake_resolve(const char *handle, char *out, size_t n, const char **why) {
    unsigned pos = 0;
    if (sscanf(handle, "@lib/1/%u", &pos) != 1) {
        *why = "not a handle";
        return false;
    }
    snprintf(out, n, "/C%u/t.wav", pos);
    return true;
}

static void settle(void) {
    for (int i = 0; i < 40 && art_service_pending(); i++) {
        art_service_run(plain_yield, NULL);
    }
}

TEST(five_browse_handles_are_decoded_together_and_a_step_only_decodes_the_new_one) {
    for (unsigned i = 0; i < 7; i++) {
        char d[600], f[700], src[700];
        snprintf(d, sizeof d, "%s/C%u", g_root, i);
        mkdir(d, 0777);
        snprintf(f, sizeof f, "%s/t.wav", d);
        FILE *w = fopen(f, "wb");
        if (w) fclose(w);
        if (i != 6) {
            snprintf(src, sizeof src, "%s/%s", NEUPOD_ART_ASSETS, i % 2 ? "q420_40x30.jpg" : "q420_500x333.jpg");
            snprintf(f, sizeof f, "%s/cover.jpg", d);
            copy(src, f);
        }
    }
    art_service_set_resolver(fake_resolve);
    const char *set1[5] = {"@lib/1/2", "@lib/1/1", "@lib/1/3", "@lib/1/0", "@lib/1/4"};
    art_view_t v;
    art_browse_get_handle(set1[0], &v);
    ASSERT_EQ(ART_VIEW_NONE, v.state); // not asked for yet
    art_browse_want_set(set1, 5);
    art_browse_get_handle(set1[0], &v);
    ASSERT_EQ(ART_VIEW_LOADING, v.state);
    ASSERT_TRUE(art_service_pending());
    art_service_run(plain_yield, NULL); // one picture per run, the centre one first
    art_browse_get_handle(set1[0], &v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    art_browse_get_handle(set1[4], &v);
    ASSERT_EQ(ART_VIEW_LOADING, v.state);
    settle();
    ASSERT_FALSE(art_service_pending());
    uint32_t gen[5];
    for (int i = 0; i < 5; i++) {
        art_browse_get_handle(set1[i], &v);
        ASSERT_EQ(ART_VIEW_READY, v.state);
        ASSERT_TRUE(v.px != NULL && v.w > 0);
        gen[i] = v.gen;
        for (int k = 0; k < i; k++) ASSERT_TRUE(gen[k] != gen[i]); // every picture is told apart
    }
    // One step to the right: four of the five are already here and keep their picture; only the new one is decoded.
    const char *set2[5] = {"@lib/1/3", "@lib/1/2", "@lib/1/4", "@lib/1/1", "@lib/1/5"};
    art_browse_want_set(set2, 5);
    art_browse_get_handle(set2[1], &v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    ASSERT_EQ(gen[0], v.gen);
    art_browse_get_handle(set2[4], &v);
    ASSERT_EQ(ART_VIEW_LOADING, v.state);
    settle();
    art_browse_get_handle(set2[4], &v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    // An album with no picture is FAILED (the placeholder stays), and is not asked for again.
    const char *set3[1] = {"@lib/1/6"};
    art_browse_want_set(set3, 1);
    settle();
    art_browse_get_handle(set3[0], &v);
    ASSERT_EQ(ART_VIEW_FAILED, v.state);
    ASSERT_FALSE(art_service_pending());
    // Nothing wanted: the answer for a handle is NONE again.
    art_browse_want_set(NULL, 0);
    settle();
    art_browse_get_handle(set3[0], &v);
    ASSERT_EQ(ART_VIEW_NONE, v.state);
    // The header's single-album calls still work on top of it.
    art_browse_want("@lib/1/2");
    settle();
    art_browse_get(&v);
    ASSERT_EQ(ART_VIEW_READY, v.state);
    art_browse_want("");
}
#endif

int main(void) {
    if (mkdtemp(g_root) == NULL || setenv("NEUPOD_FS_ROOT", g_root, 1) != 0) return 1;
    RUN(retargeting_during_a_decode_never_shows_the_old_cover_for_the_new_track);
#ifdef NEUPOD_ART_HIRES
    RUN(an_album_played_before_comes_back_from_the_cache_without_decoding);
    RUN(five_browse_handles_are_decoded_together_and_a_step_only_decodes_the_new_one);
#endif
    return RESULT();
}
