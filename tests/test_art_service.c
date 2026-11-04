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

int main(void) {
    if (mkdtemp(g_root) == NULL || setenv("NEUPOD_FS_ROOT", g_root, 1) != 0) return 1;
    RUN(retargeting_during_a_decode_never_shows_the_old_cover_for_the_new_track);
    return RESULT();
}
