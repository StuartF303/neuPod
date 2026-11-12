// SPDX-License-Identifier: MPL-2.0
// The ratings store (ratings_store.h): keys, the table and its eviction, the file image and its checks. The card side (load / save under the write
// scope) is the settings store's pattern and runs on the storage task; here only the pure part.
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings_store.h"

#include "ratings_store.h"
#include "unit.h"

TEST(a_key_depends_on_the_path_and_the_size) {
    uint32_t a = ratings_key("/Music/A/01.flac", 1000), b = ratings_key("/Music/A/01.flac", 1001), c = ratings_key("/Music/A/02.flac", 1000);
    ASSERT_TRUE(a != 0 && b != 0 && c != 0);
    ASSERT_TRUE(a != b && a != c);
    ASSERT_EQ(a, ratings_key("/Music/A/01.flac", 1000)); /* stable: it is what a rebuilt library finds again */
    ASSERT_TRUE(ratings_key("/Music/A/01.flac", 5000000000ull) != a); /* sizes above 4 GiB count too */
}

TEST(set_get_clear_and_range) {
    ratings_reset();
    uint32_t k = ratings_key("/a.flac", 10);
    ASSERT_EQ(0, ratings_get(k));
    ratings_set(k, 4);
    ASSERT_EQ(4, ratings_get(k));
    ratings_set(k, 6); /* out of range: ignored */
    ASSERT_EQ(4, ratings_get(k));
    ratings_set(k, 0); /* clears */
    ASSERT_EQ(0, ratings_get(k));
    ratings_set(0, 3); /* key 0 is "free" */
    ASSERT_EQ(0, ratings_get(0));
}

TEST(the_least_recently_rated_is_dropped_when_full) {
    ratings_reset();
    for (uint32_t i = 1; i <= RATINGS_MAX; i++) ratings_set(i, 1);
    ratings_set(1, 2); /* 1 is the freshest now */
    ratings_set(RATINGS_MAX + 1, 5);
    ASSERT_EQ(5, ratings_get(RATINGS_MAX + 1));
    ASSERT_EQ(2, ratings_get(1));
    ASSERT_EQ(0, ratings_get(2)); /* the oldest went */
}

TEST(the_image_round_trips_and_a_bad_one_is_refused) {
    ratings_reset();
    ratings_set(11, 3);
    ratings_set(22, 5);
    uint8_t img[12 + RATINGS_MAX * 5 + 4];
    size_t n = ratings_serialise(img, sizeof img);
    ASSERT_EQ(12 + 2 * 5 + 4, n);
    ratings_reset();
    ASSERT_TRUE(ratings_parse(img, n));
    ASSERT_EQ(3, ratings_get(11));
    ASSERT_EQ(5, ratings_get(22));
    img[13] ^= 1; /* a flipped bit: the CRC refuses it and the table is untouched */
    ASSERT_FALSE(ratings_parse(img, n));
    ASSERT_EQ(3, ratings_get(11));
    ASSERT_FALSE(ratings_parse(img, 5));
    ASSERT_EQ(0, ratings_serialise(img, 8)); /* too small a buffer */
}

static char g_dir[300], g_bin[320], g_tmp[320];

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
    ratings_reset();
}

TEST(a_save_goes_through_a_temp_file_and_survives_a_cut) {
    unlink(g_bin);
    unlink(g_tmp);
    boot_card();
    ratings_set(11, 3);
    ratings_poll(0);
    ASSERT_TRUE(ratings_persistent());
    struct stat st;
    ASSERT_EQ(0, stat(g_bin, &st));
    ASSERT_TRUE(stat(g_tmp, &st) != 0); /* no temp file left behind */
    uint8_t good[12 + RATINGS_MAX * 5 + 4];
    size_t gn = ratings_serialise(good, sizeof good);

    ratings_set(22, 5); /* a second save, replacing the first */
    ratings_poll(0);
    ASSERT_TRUE(stat(g_tmp, &st) != 0);
    boot_card();
    ratings_poll(0);
    ASSERT_EQ(3, ratings_get(11));
    ASSERT_EQ(5, ratings_get(22));

    /* cut while the temp file was half written: the main file still wins */
    put_file(g_tmp, good, gn / 2);
    boot_card();
    ratings_poll(0);
    ASSERT_EQ(3, ratings_get(11));
    ASSERT_EQ(5, ratings_get(22));
    unlink(g_tmp);

    /* cut after the old file was removed but before the rename: the complete temp file is loaded */
    unlink(g_bin);
    put_file(g_tmp, good, gn);
    boot_card();
    ratings_poll(0);
    ASSERT_EQ(3, ratings_get(11));
    ASSERT_EQ(0, ratings_get(22));
    unlink(g_bin);
    unlink(g_tmp);
}

int main(void) {
    char tpl[] = "neupod_ratings_test_XXXXXX";
    if (mkdtemp(tpl) == NULL) return 2;
    char abs[200];
    if (realpath(tpl, abs) == NULL) return 2;
    setenv("NEUPOD_FS_ROOT", abs, 1);
    snprintf(g_dir, sizeof g_dir, "%s/.neupod", abs);
    snprintf(g_bin, sizeof g_bin, "%s/ratings.bin", g_dir);
    snprintf(g_tmp, sizeof g_tmp, "%s/ratings.tmp", g_dir);
    mkdir(g_dir, 0755);
    RUN(a_key_depends_on_the_path_and_the_size);
    RUN(set_get_clear_and_range);
    RUN(the_least_recently_rated_is_dropped_when_full);
    RUN(the_image_round_trips_and_a_bad_one_is_refused);
    RUN(a_save_goes_through_a_temp_file_and_survives_a_cut);
    unlink(g_bin);
    unlink(g_tmp);
    unlink(g_dir);
    rmdir(g_dir);
    rmdir(abs);
    return RESULT();
}
