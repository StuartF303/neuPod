// SPDX-License-Identifier: MPL-2.0
// Resume record (app/runtime/resume.c) over the real settings store on the host "card": save, flush through the store's own write path
// (write lock opened only inside settings_flush), reboot, load; rate limiting; the missing / changed file decisions.
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal_storage.h"
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
    s.size = 123456789u;
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
    ASSERT_EQ(123456789u, out.size);
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
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", abs);
    int rc = system(cmd);
    (void)rc;
    return RESULT();
}
