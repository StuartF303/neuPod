// SPDX-License-Identifier: MPL-2.0
// The plays store (plays_store.h): counting, the table's eviction, the file and its checks, and the save through a temp file that survives a power cut.
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plays_store.h"
#include "ratings_store.h"
#include "settings_store.h"
#include "unit.h"

static char g_dir[300], g_bin[320], g_tmp[320];

static uint32_t key_of(const char *path, uint64_t size) { return ratings_key(path, size); }

static plays_entry_t find(const char *path, uint64_t size) {
    plays_entry_t e, none = {0};
    for (uint32_t i = 0; i < plays_slots(); i++) {
        if (plays_at(i, &e) && e.key == key_of(path, size)) {
            return e;
        }
    }
    return none;
}

static uint32_t entries(void) {
    uint32_t n = 0;
    plays_entry_t e;
    for (uint32_t i = 0; i < plays_slots(); i++) {
        n += plays_at(i, &e);
    }
    return n;
}

TEST(a_play_is_counted_and_a_note_is_not) {
    plays_reset();
    ASSERT_TRUE(plays_record("/Music/A/01.flac", 1000, "First", "Band", true));
    ASSERT_TRUE(plays_record("/Music/A/01.flac", 1000, "First", "Band", true));
    ASSERT_TRUE(plays_record("/Music/A/02.flac", 2000, "Second", "Band", false)); /* a rating / On-The-Go add: known, never played */
    plays_entry_t a = find("/Music/A/01.flac", 1000), b = find("/Music/A/02.flac", 2000);
    ASSERT_EQ(2, a.plays);
    ASSERT_TRUE(strcmp(a.title, "First") == 0 && strcmp(a.artist, "Band") == 0 && strcmp(a.path, "/Music/A/01.flac") == 0);
    ASSERT_TRUE(a.last != 0);
    ASSERT_EQ(0, b.plays);
    ASSERT_EQ(0, b.last);
    ASSERT_EQ(2, plays_get(key_of("/Music/A/01.flac", 1000)));
    ASSERT_EQ(0, plays_get(key_of("/nothing.flac", 1)));
    ASSERT_EQ(2, entries());
    ASSERT_TRUE(b.stamp > a.last); /* serials only go up: the note came after the last play */
}

TEST(the_same_path_with_another_size_is_another_track_and_odd_input_is_ignored) {
    plays_reset();
    ASSERT_TRUE(plays_record("/a.flac", 10, "T", "A", true));
    ASSERT_TRUE(plays_record("/a.flac", 11, "T", "A", true));
    ASSERT_EQ(2, entries());
    ASSERT_FALSE(plays_record("relative.flac", 1, "T", "A", true));
    ASSERT_FALSE(plays_record(NULL, 1, "T", "A", true));
    char longp[200];
    memset(longp, 'x', sizeof longp);
    longp[0] = '/';
    longp[sizeof longp - 1] = '\0';
    ASSERT_FALSE(plays_record(longp, 1, "T", "A", true)); /* not tracked: it would not fit */
    ASSERT_EQ(2, entries());
}

TEST(long_titles_are_cut_at_a_character_boundary) {
    plays_reset();
    char t[100];
    memset(t, 'a', sizeof t);
    t[sizeof t - 1] = '\0';
    t[PLAYS_TITLE_MAX - 3] = '\xC3'; /* a 2-byte character straddling the limit */
    t[PLAYS_TITLE_MAX - 2] = '\xA9';
    ASSERT_TRUE(plays_record("/a.flac", 1, t, "", true));
    plays_entry_t e = find("/a.flac", 1);
    size_t n = strlen(e.title);
    ASSERT_TRUE(n < PLAYS_TITLE_MAX);
    ASSERT_TRUE(((unsigned char)e.title[n - 1] & 0xC0u) != 0x80u || n < 2 || ((unsigned char)e.title[n - 2] & 0xC0u) == 0xC0u);
}

TEST(a_full_table_drops_the_least_played_and_keeps_rated_ones) {
    plays_reset();
    ratings_reset();
    char p[40];
    for (uint32_t i = 0; i < PLAYS_MAX; i++) {
        snprintf(p, sizeof p, "/t/%03u.flac", (unsigned)i);
        plays_record(p, i + 1, "T", "A", true);
        if (i >= 1) {
            (void)plays_get(1); /* the storage task folds the inbox in between plays; the test does it by asking */
            plays_record(p, i + 1, "T", "A", true); /* played twice */
        }
        (void)plays_get(1);
    }
    ASSERT_EQ(PLAYS_MAX, entries());
    ratings_set(key_of("/t/000.flac", 1), 5); /* the one played once is rated: it must stay */
    plays_record("/new.flac", 5, "N", "A", true);
    (void)plays_get(1);
    ASSERT_EQ(PLAYS_MAX, entries());
    ASSERT_EQ(1, find("/t/000.flac", 1).plays);
    ASSERT_EQ(1, find("/new.flac", 5).plays);
    ASSERT_EQ(0, find("/t/001.flac", 2).plays); /* the oldest of the twice-played went instead */
    ratings_reset();
}

TEST(the_file_round_trips_and_a_damaged_one_leaves_the_table_alone) {
    plays_reset();
    plays_record("/Music/A/01.flac", 1000, "First", "Band", true);
    plays_record("/Music/A/01.flac", 1000, "First", "Band", true);
    plays_record("/Music/B/02.mp3", 2000, "Caf\xC3\xA9", "Artist", true);
    plays_record("/Music/C/03.wav", 3000, "Rated only", "", false);
    unlink(g_bin);
    ASSERT_TRUE(plays_save_file(PLAYS_FILE));
    plays_reset();
    ASSERT_TRUE(plays_load_file(PLAYS_FILE));
    ASSERT_EQ(3, entries());
    plays_entry_t a = find("/Music/A/01.flac", 1000);
    ASSERT_EQ(2, a.plays);
    ASSERT_TRUE(strcmp(find("/Music/B/02.mp3", 2000).title, "Caf\xC3\xA9") == 0);
    ASSERT_EQ(0, find("/Music/C/03.wav", 3000).plays);
    /* a flipped bit: refused, and what the table holds stays */
    int fd = open(g_bin, O_RDWR);
    ASSERT_TRUE(fd >= 0);
    unsigned char b = 0;
    ASSERT_EQ(1, pread(fd, &b, 1, 40));
    b ^= 0x10;
    ASSERT_EQ(1, pwrite(fd, &b, 1, 40));
    close(fd);
    ASSERT_FALSE(plays_load_file(PLAYS_FILE));
    ASSERT_EQ(3, entries());
    ASSERT_FALSE(plays_load_file("/missing/file.bin"));
    /* a truncated file */
    ASSERT_TRUE(plays_save_file(PLAYS_FILE));
    ASSERT_EQ(0, truncate(g_bin, 30));
    ASSERT_FALSE(plays_load_file(PLAYS_FILE));
    unlink(g_bin);
}

static void put_file(const char *path, const uint8_t *d, size_t n) {
    FILE *f = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ(n, fwrite(d, 1, n, f));
    fclose(f);
}
static void boot_card(void) {
    settings_init(NULL);
    settings_attach_storage(true);
    (void)settings_load();
    plays_reset();
    ratings_reset();
}
static size_t slurp(const char *path, uint8_t *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

TEST(a_save_goes_through_a_temp_file_and_survives_a_cut) {
    unlink(g_bin);
    unlink(g_tmp);
    boot_card();
    plays_record("/a.flac", 1, "A", "X", true);
    plays_poll(0);
    ASSERT_TRUE(plays_persistent());
    struct stat st;
    ASSERT_EQ(0, stat(g_bin, &st));
    ASSERT_TRUE(stat(g_tmp, &st) != 0); /* no temp file left behind */
    static uint8_t good[40000];
    size_t gn = slurp(g_bin, good, sizeof good);
    ASSERT_TRUE(gn > 20);

    plays_record("/b.flac", 2, "B", "X", true); /* a second save replaces the first */
    plays_poll(0);
    ASSERT_TRUE(stat(g_tmp, &st) != 0);
    boot_card();
    plays_poll(0);
    ASSERT_EQ(1, plays_get(key_of("/a.flac", 1)));
    ASSERT_EQ(1, plays_get(key_of("/b.flac", 2)));

    /* cut while the temp file was half written: the main file still wins */
    put_file(g_tmp, good, gn / 2);
    boot_card();
    plays_poll(0);
    ASSERT_EQ(1, plays_get(key_of("/a.flac", 1)));
    ASSERT_EQ(1, plays_get(key_of("/b.flac", 2)));
    unlink(g_tmp);

    /* cut after the old file was removed but before the rename: the complete temp file is loaded */
    unlink(g_bin);
    put_file(g_tmp, good, gn);
    boot_card();
    plays_poll(0);
    ASSERT_EQ(1, plays_get(key_of("/a.flac", 1)));
    ASSERT_EQ(0, plays_get(key_of("/b.flac", 2)));
    unlink(g_bin);
    unlink(g_tmp);
}

TEST(plays_noted_before_the_card_is_ready_add_to_what_the_file_holds) {
    unlink(g_bin);
    unlink(g_tmp);
    boot_card();
    plays_record("/a.flac", 1, "A", "X", true);
    plays_record("/a.flac", 1, "A", "X", true);
    plays_poll(0);
    /* a new boot: one play noted while the card was not attached yet, then the card appears */
    settings_init(NULL);
    settings_attach_storage(false);
    (void)settings_load();
    plays_reset();
    plays_record("/a.flac", 1, "A", "X", true);
    plays_poll(0); /* no card: RAM only */
    ASSERT_FALSE(plays_persistent());
    ASSERT_EQ(1, plays_get(key_of("/a.flac", 1)));
    settings_attach_storage(true);
    (void)settings_load();
    plays_poll(0);
    ASSERT_TRUE(plays_persistent());
    ASSERT_EQ(3, plays_get(key_of("/a.flac", 1))); /* 2 from the file + 1 from this session */
    unlink(g_bin);
    unlink(g_tmp);
}

int main(void) {
    char tpl[] = "neupod_plays_test_XXXXXX";
    if (mkdtemp(tpl) == NULL) return 2;
    char abs[200];
    if (realpath(tpl, abs) == NULL) return 2;
    setenv("NEUPOD_FS_ROOT", abs, 1);
    snprintf(g_dir, sizeof g_dir, "%s/.neupod", abs);
    snprintf(g_bin, sizeof g_bin, "%s/plays.bin", g_dir);
    snprintf(g_tmp, sizeof g_tmp, "%s/plays.tmp", g_dir);
    mkdir(g_dir, 0755);
    RUN(a_play_is_counted_and_a_note_is_not);
    RUN(the_same_path_with_another_size_is_another_track_and_odd_input_is_ignored);
    RUN(long_titles_are_cut_at_a_character_boundary);
    RUN(a_full_table_drops_the_least_played_and_keeps_rated_ones);
    RUN(the_file_round_trips_and_a_damaged_one_leaves_the_table_alone);
    RUN(a_save_goes_through_a_temp_file_and_survives_a_cut);
    RUN(plays_noted_before_the_card_is_ready_add_to_what_the_file_holds);
    unlink(g_bin);
    unlink(g_tmp);
    rmdir(g_dir);
    rmdir(abs);
    return RESULT();
}
