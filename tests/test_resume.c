// SPDX-License-Identifier: MPL-2.0
// Resume record (app/runtime/resume.c) over the real settings store on the host "card": save, flush through the store's own write path
// (write lock opened only inside settings_flush), reboot, load; rate limiting; the missing / changed file decisions.
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal_storage.h"
#include "meta.h"
#include "nfs.h"
#include "resume.h"
#include "settings_store.h"
#include "unit.h"

static char g_tpl[] = "neupod_resume_test_XXXXXX";

static settings_load_result_t boot(void) {
    settings_init(NULL);
    settings_attach_storage(true);
    return settings_load();
}

static resume_state_t sample(const char *path, uint32_t frame) {
    resume_state_t s;
    memset(&s, 0, sizeof s);
    snprintf(s.path, sizeof s.path, "%s", path);
    s.index = 7;
    s.frame = frame;
    s.size = 0x123456789ull; // beyond 4 GiB
    s.playing = true;
    return s;
}

TEST(save_flush_reboot_load_round_trip) {
    ASSERT_EQ(SETTINGS_LOAD_FRESH, boot());
    resume_state_t out;
    ASSERT_FALSE(resume_load(&out));
    resume_state_t s = sample("/Music/Some Album/03 A Song.flac", 44100u * 61u);
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 1000));
    ASSERT_TRUE(settings_dirty());
    ASSERT_EQ(0, settings_flush());      // the store's own write path
    ASSERT_FALSE(hal_storage_get_write_enabled()); // the card is locked again
    ASSERT_EQ(SETTINGS_LOAD_OK, boot()); // "reboot"
    ASSERT_TRUE(resume_load(&out));
    ASSERT_EQ(0, strcmp(out.path, s.path));
    ASSERT_EQ(7, out.index);
    ASSERT_EQ(44100u * 61u, out.frame);
    ASSERT_EQ(0x123456789ull, out.size);
    ASSERT_TRUE(out.playing);
    s.playing = false;
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 2000));
    ASSERT_EQ(0, settings_flush());
    boot();
    ASSERT_TRUE(resume_load(&out));
    ASSERT_FALSE(out.playing);
}

TEST(long_path_uses_both_items_and_too_long_is_refused) {
    boot();
    char path[RESUME_PATH_MAX];
    size_t n = RESUME_PATH_MAX_STORED;
    path[0] = '/';
    for (size_t i = 1; i < n; i++) path[i] = (char)('a' + i % 26);
    path[n - 6] = '/';
    memcpy(path + n - 5, ".flac", 6);
    ASSERT_EQ(n, strlen(path));
    resume_state_t s = sample(path, 5), out;
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 5000));
    ASSERT_EQ(0, settings_flush());
    boot();
    ASSERT_TRUE(resume_load(&out));
    ASSERT_EQ(0, strcmp(out.path, path));
    // one byte too long: not stored, and the old record is dropped rather than left to resume the wrong track
    path[n] = 'x';
    path[n + 1] = '\0';
    s = sample(path, 5);
    ASSERT_FALSE(resume_save(&s, RESUME_EVENT, 6000));
    ASSERT_FALSE(resume_load(&out));
    // a short path afterwards removes the second item again
    s = sample("/a.wav", 1);
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 7000));
    ASSERT_TRUE(resume_load(&out));
    ASSERT_EQ(0, strcmp(out.path, "/a.wav"));
    ASSERT_EQ(0, settings_get(SETTING_RESUME1, NULL, 0));
}

TEST(periodic_updates_are_rate_limited_events_are_not) {
    boot();
    resume_state_t s = sample("/A/x.mp3", 100);
    uint32_t t = 0xFFFFFF00u; // the 32-bit tick wraps during the test
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, t));
    s.frame = 200;
    ASSERT_FALSE(resume_periodic_due((uint32_t)(t + RESUME_PERIOD_MS - 1u)));
    ASSERT_FALSE(resume_save(&s, RESUME_PERIODIC, (uint32_t)(t + RESUME_PERIOD_MS - 1u)));
    resume_state_t out;
    ASSERT_TRUE(resume_load(&out));
    ASSERT_EQ(100, out.frame);           // the early periodic update changed nothing
    ASSERT_TRUE(resume_periodic_due((uint32_t)(t + RESUME_PERIOD_MS)));
    ASSERT_TRUE(resume_save(&s, RESUME_PERIODIC, (uint32_t)(t + RESUME_PERIOD_MS)));
    resume_load(&out);
    ASSERT_EQ(200, out.frame);
    s.frame = 300;
    ASSERT_FALSE(resume_save(&s, RESUME_PERIODIC, (uint32_t)(t + RESUME_PERIOD_MS + 10u))); // the limit restarts from the last stored one
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, (uint32_t)(t + RESUME_PERIOD_MS + 10u)));      // a pause is always stored
    resume_load(&out);
    ASSERT_EQ(300, out.frame);
    // 30 s of periodic polling with an unchanged position does not even dirty the store (the card is spared)
    ASSERT_EQ(0, settings_flush());
    ASSERT_TRUE(resume_save(&s, RESUME_PERIODIC, (uint32_t)(t + 3u * RESUME_PERIOD_MS)));
    ASSERT_FALSE(settings_dirty());
}

TEST(damaged_records_do_not_load) {
    boot();
    resume_state_t s = sample("/A/x.mp3", 100), out;
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 1));
    uint8_t h[SETTINGS_VALUE_MAX];
    uint32_t len = settings_get(SETTING_RESUME0, h, sizeof h);
    h[0] = 9; // another version
    ASSERT_TRUE(settings_put(SETTING_RESUME0, h, len));
    ASSERT_FALSE(resume_load(&out));
    h[0] = 1;
    h[12] = 40; // the path length disagrees with what is stored
    ASSERT_TRUE(settings_put(SETTING_RESUME0, h, len));
    ASSERT_FALSE(resume_load(&out));
    resume_clear();
    ASSERT_FALSE(resume_load(&out));
    ASSERT_FALSE(resume_save(&(resume_state_t){.path = "relative/x.wav"}, RESUME_EVENT, 1)); // not absolute
}

TEST(split_path) {
    char dir[64];
    const char *name;
    ASSERT_TRUE(resume_split("/Music/Album/01 x.flac", dir, sizeof dir, &name));
    ASSERT_EQ(0, strcmp(dir, "/Music/Album"));
    ASSERT_EQ(0, strcmp(name, "01 x.flac"));
    ASSERT_TRUE(resume_split("/x.wav", dir, sizeof dir, &name));
    ASSERT_EQ(0, strcmp(dir, "/"));
    ASSERT_EQ(0, strcmp(name, "x.wav"));
    ASSERT_FALSE(resume_split("/Music/", dir, sizeof dir, &name));
    ASSERT_FALSE(resume_split("/Music/Album/01 x.flac", dir, 5, &name));
}

TEST(plan_for_a_missing_or_changed_file) {
    file_entry_t e[] = {{.name = "Art", .is_dir = true}, {.name = "01 a.flac", .size = 1000}, {.name = "cover.jpg", .size = 5},
                        {.name = "02 b.flac", .size = 2000}, {.name = "03 c.mp3", .size = 3000}};
    resume_state_t r = sample("/Album/02 b.flac", 777);
    r.index = 1;
    r.size = 2000;
    uint32_t idx = 99, frame = 99;
    ASSERT_EQ(RESUME_PLAN_EXACT, resume_plan(&r, "02 b.flac", e, 5, &idx, &frame));
    ASSERT_EQ(3, idx);
    ASSERT_EQ(777, frame);
    // replaced by a file of another size: same track from its beginning
    r.size = 2001;
    ASSERT_EQ(RESUME_PLAN_CHANGED, resume_plan(&r, "02 b.flac", e, 5, &idx, &frame));
    ASSERT_EQ(3, idx);
    ASSERT_EQ(0, frame);
    // unknown saved size: not compared
    r.size = 0;
    ASSERT_EQ(RESUME_PLAN_EXACT, resume_plan(&r, "02 b.flac", e, 5, &idx, &frame));
    ASSERT_EQ(777, frame);
    // gone: the playable entry at the saved queue position (here 1 = the second playable file), from its beginning
    r.size = 2000;
    ASSERT_EQ(RESUME_PLAN_GONE, resume_plan(&r, "02 renamed.flac", e, 5, &idx, &frame));
    ASSERT_EQ(3, idx);
    ASSERT_EQ(0, frame);
    r.index = 40; // beyond the folder: the last playable one
    ASSERT_EQ(RESUME_PLAN_GONE, resume_plan(&r, "gone.flac", e, 5, &idx, &frame));
    ASSERT_EQ(4, idx);
    // a name that is a folder, or not audio, is not the file
    ASSERT_EQ(RESUME_PLAN_GONE, resume_plan(&r, "Art", e, 5, &idx, &frame));
    ASSERT_EQ(RESUME_PLAN_GONE, resume_plan(&r, "cover.jpg", e, 5, &idx, &frame));
    // nothing playable left
    ASSERT_EQ(RESUME_PLAN_NOTHING, resume_plan(&r, "02 b.flac", e, 1, &idx, &frame)); // only a folder is left
    ASSERT_EQ(RESUME_PLAN_NOTHING, resume_plan(&r, "x.flac", e, 0, &idx, &frame));
}

TEST(a_torn_pair_does_not_load) {
    boot();
    char a[RESUME_PATH_MAX], b[RESUME_PATH_MAX];
    snprintf(a, sizeof a, "/%0*d/a.flac", 150, 1);
    snprintf(b, sizeof b, "/%0*d/b.flac", 150, 2);
    resume_state_t sa = sample(a, 1), sb = sample(b, 2), out;
    ASSERT_TRUE(resume_save(&sa, RESUME_EVENT, 1));
    uint8_t second_a[SETTINGS_VALUE_MAX], first_a[SETTINGS_VALUE_MAX];
    uint32_t la = settings_get(SETTING_RESUME1, second_a, sizeof second_a);
    uint32_t fa = settings_get(SETTING_RESUME0, first_a, sizeof first_a);
    ASSERT_TRUE(resume_save(&sb, RESUME_EVENT, 2));
    ASSERT_TRUE(resume_load(&out));
    ASSERT_EQ(0, strcmp(out.path, b));
    // first half of B with the second half of A (a save or a read caught between the two puts): refused
    ASSERT_TRUE(settings_put(SETTING_RESUME1, second_a, la));
    ASSERT_FALSE(resume_load(&out));
    // and the other way round
    ASSERT_TRUE(resume_save(&sb, RESUME_EVENT, 3));
    ASSERT_TRUE(settings_put(SETTING_RESUME0, first_a, fa));
    ASSERT_FALSE(resume_load(&out));
}

TEST(resume_yields_when_the_store_is_nearly_full) {
    boot();
    resume_clear(); // (boot() loaded the earlier tests' record)
    resume_state_t s = sample("/A/x.flac", 1), out;
    uint32_t i = 0;
    for (;; i++) { // fill all but 3 slots
        settings_status_t st;
        settings_get_status(&st);
        if (st.items + 3u >= SETTINGS_MAX_ITEMS) break;
        ASSERT_TRUE(settings_put(SETTINGS_TAG('F', 'I', 'L', (char)('A' + i)), "x", 1));
    }
    ASSERT_FALSE(resume_save(&s, RESUME_EVENT, 1)); // would leave fewer than 4 free: not taken
    ASSERT_FALSE(resume_load(&out));
    settings_delete(SETTINGS_TAG('F', 'I', 'L', 'A'));
    settings_delete(SETTINGS_TAG('F', 'I', 'L', 'B'));
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 2)); // room again
    s.frame = 9;
    ASSERT_TRUE(resume_save(&s, RESUME_EVENT, 3)); // updating an existing record needs no new slot
}

// ---- MP3 seek landing: a synthetic VBR file (Xing table of contents) with frames of different sizes ----
typedef struct { uint8_t *d; size_t n; } mem_t;
static int32_t mem_rd(void *c, uint64_t off, void *dst, uint32_t n) {
    const mem_t *m = c;
    if (off >= m->n) return 0;
    size_t k = off + n > m->n ? m->n - (size_t)off : n;
    memcpy(dst, m->d + off, k);
    return (int32_t)k;
}

TEST(mp3_seek_lands_where_it_says_cbr_and_vbr_with_toc) {
    // MPEG-1 layer 3, 44.1 kHz stereo, no CRC: header FF FB <bitrate idx><44.1k> 00; frame bytes = 144000*kbps/44100
    static const uint8_t bi[4] = {0x90, 0xB0, 0xD0, 0xE0}; // 128, 160, 224, 256 kbps
    static const uint32_t kb[4] = {128, 160, 224, 256};
    enum { FRAMES = 2000 };
    static uint8_t buf[2000 * 836 + 4096];
    static uint32_t start[FRAMES + 1];
    for (int vbr = 0; vbr < 2; vbr++) {
        size_t pos = 0;
        // frame 0: the Xing (VBR) or Info (CBR) frame, 128 kbps = 417 bytes (the table of contents needs room)
        memset(buf, 0, sizeof buf);
        buf[0] = 0xFF; buf[1] = 0xFB; buf[2] = 0x90; buf[3] = 0x00;
        pos = 417;
        for (int f = 0; f < FRAMES; f++) {
            int k = vbr ? (f / 7 + f * 3) % 4 : 0;
            start[f] = (uint32_t)pos;
            uint32_t len = 144000u * kb[k] / 44100u;
            buf[pos] = 0xFF; buf[pos + 1] = 0xFB; buf[pos + 2] = bi[k]; buf[pos + 3] = 0x00;
            pos += len;
        }
        start[FRAMES] = (uint32_t)pos;
        uint32_t audio = (uint32_t)pos - 417u; // bytes after the Xing frame... the table is relative to the audio from the Xing frame on
        uint8_t *x = buf + 4 + 32;
        memcpy(x, vbr ? "Xing" : "Info", 4);
        x[7] = 1 | 2 | 4; // frames, bytes, TOC
        x[8] = FRAMES >> 24; x[9] = FRAMES >> 16; x[10] = FRAMES >> 8; x[11] = FRAMES;
        uint32_t tb = (uint32_t)pos;
        x[12] = tb >> 24; x[13] = tb >> 16; x[14] = tb >> 8; x[15] = tb;
        for (int p = 0; p < 100; p++) { // byte position (of the whole file from the Xing frame) at p percent, in 1/256 of the file
            uint32_t fr = (uint32_t)((uint64_t)p * FRAMES / 100);
            x[16 + p] = (uint8_t)((uint64_t)start[fr] * 256u / tb);
        }
        (void)audio;
        mem_t m = {buf, pos};
        uint64_t worst = 0;
        for (uint64_t want = 50; want < FRAMES * 1152ull - 2000; want += 24137) {
            uint64_t at = 0, landed = 0;
            ASSERT_TRUE(mp3_seek_point(mem_rd, &m, pos, 0, (uint64_t)FRAMES * 1152u, want, &at, &landed));
            ASSERT_TRUE(buf[at] == 0xFF && (buf[at + 1] & 0xE0) == 0xE0);
            // the true frame of the landed offset
            uint32_t tf = 0;
            while (tf + 1 < FRAMES && start[tf + 1] <= at) tf++;
            uint64_t truth = (uint64_t)tf * 1152u;
            uint64_t err = truth > landed ? truth - landed : landed - truth;
            if (err > worst) worst = err;
        }
        // CBR: a frame or so; VBR with the table: the reported frame is within 1.5% of the track of the real one
        printf("  mp3 %s: worst landed-frame error %llu frames (track %u)\n", vbr ? "VBR" : "CBR", (unsigned long long)worst, FRAMES * 1152u);
        ASSERT_TRUE(worst <= (vbr ? (uint64_t)FRAMES * 1152u * 15u / 1000u : 2u * 1152u));
    }
}

TEST(alac_seek_matches_walking_the_packets) {
    static const char *const files[] = {"alac16_44k_st.m4a", "alac16_48k_mono.m4a", "alac24_48k_st.m4a"};
    for (unsigned fi = 0; fi < sizeof files / sizeof files[0]; fi++) {
        char path[600];
        snprintf(path, sizeof path, "%s/%s", NEUPOD_TEST_ASSETS, files[fi]);
        FILE *f = fopen(path, "rb");
        ASSERT_TRUE(f != NULL);
        if (f == NULL) continue;
        static uint8_t buf[200000];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        mem_t m = {buf, n};
        mp4_info_t info;
        ASSERT_EQ(META_OK, mp4_parse(mem_rd, &m, n, &info));
        ASSERT_TRUE(info.sample_count > 2);
        for (uint32_t k = 0; k < info.sample_count; k++) {
            mp4_iter_t a, b;
            mp4_iter_init(&a, &info);
            uint64_t oa = 0, ob = 0;
            uint32_t sa = 0, sb = 0;
            for (uint32_t i = 0; i <= k; i++) ASSERT_TRUE(mp4_iter_next(&a, mem_rd, &m, &oa, &sa));
            mp4_iter_init(&b, &info);
            ASSERT_TRUE(mp4_iter_seek(&b, mem_rd, &m, k));
            ASSERT_TRUE(mp4_iter_next(&b, mem_rd, &m, &ob, &sb));
            ASSERT_TRUE(oa == ob && sa == sb);
            if (k + 1 < info.sample_count) { // and the one after it (the chunk boundary logic)
                ASSERT_TRUE(mp4_iter_next(&a, mem_rd, &m, &oa, &sa));
                ASSERT_TRUE(mp4_iter_next(&b, mem_rd, &m, &ob, &sb));
                ASSERT_TRUE(oa == ob && sa == sb);
            }
        }
        mp4_iter_t z;
        mp4_iter_init(&z, &info);
        ASSERT_FALSE(mp4_iter_seek(&z, mem_rd, &m, info.sample_count));
    }
}

TEST(write_while_playing_gate_math) {
    // 32768 bytes of 32-bit stereo at 48 kHz = 4096 frames = 85 ms; the PCM ring adds its low-water mark
    ASSERT_EQ(85, resume_buffered_ms(48000, 32768, 8, 0));            // 4096 frames / 48 kHz
    ASSERT_EQ(92, resume_buffered_ms(44100, 32768, 8, 0));            // 4096 / 44.1 kHz
    ASSERT_EQ(185, resume_buffered_ms(44100, 32768, 8, 4096));        // plus a full PCM ring
    ASSERT_EQ(0, resume_buffered_ms(0, 32768, 8, 100));
    ASSERT_EQ(0, resume_buffered_ms(48000, 32768, 0, 100));
    ASSERT_FALSE(resume_write_while_playing_ok(resume_buffered_ms(96000, 32768, 8, 4096))); // 24/96: 42 + 42 ms
    ASSERT_FALSE(resume_write_while_playing_ok(299));
    ASSERT_TRUE(resume_write_while_playing_ok(300));
    ASSERT_TRUE(resume_write_while_playing_ok(resume_buffered_ms(44100, 32768, 2, 4096))); // 16-bit mono data: 371 ms + 93 ms
}

int main(void) {
    if (mkdtemp(g_tpl) == NULL) {
        perror("mkdtemp");
        return 2;
    }
    char abs[200];
    if (realpath(g_tpl, abs) == NULL) {
        return 2;
    }
    setenv("NEUPOD_FS_ROOT", abs, 1);
    RUN(save_flush_reboot_load_round_trip);
    RUN(long_path_uses_both_items_and_too_long_is_refused);
    RUN(periodic_updates_are_rate_limited_events_are_not);
    RUN(damaged_records_do_not_load);
    RUN(split_path);
    RUN(plan_for_a_missing_or_changed_file);
    RUN(a_torn_pair_does_not_load);
    RUN(resume_yields_when_the_store_is_nearly_full);
    RUN(mp3_seek_lands_where_it_says_cbr_and_vbr_with_toc);
    RUN(alac_seek_matches_walking_the_packets);
    RUN(write_while_playing_gate_math);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", abs);
    int rc = system(cmd);
    (void)rc;
    return RESULT();
}
