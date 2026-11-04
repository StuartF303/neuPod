// SPDX-License-Identifier: MPL-2.0
// Settings store on the host (POSIX nfs backend): round trip, CRC failure, RAM-only fallback, power loss at every step
// of the atomic save, the write scope, capacity limits, the BTstack storage adapter and the write-lock discipline.
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hal_storage.h"
#include "nfs.h"
#include "settings_store.h"
#include "unit.h"

static char g_tpl[] = "neupod_settings_test_XXXXXX";
static char g_root[256];
static char g_dir[256], g_bin[300], g_tmp[300];

static int g_lock_depth, g_lock_calls;
static void test_lock(void *c) {
    (void)c;
    g_lock_depth++;
    g_lock_calls++;
}
static void test_unlock(void *c) {
    (void)c;
    g_lock_depth--;
}
static const settings_lock_t k_lock = {.lock = test_lock, .unlock = test_unlock};

static void rm_state(void) {
    unlink(g_bin);
    unlink(g_tmp);
    rmdir(g_dir);
}

// Fresh RAM store attached to the "card" and loaded.
static settings_load_result_t boot(bool card) {
    settings_init(&k_lock);
    settings_attach_storage(card);
    return settings_load();
}

static void file_corrupt_byte(const char *path, long at) {
    int fd = open(path, O_RDWR);
    ASSERT_TRUE(fd >= 0);
    unsigned char b = 0;
    ASSERT_EQ(1, pread(fd, &b, 1, at));
    b ^= 0x5A;
    ASSERT_EQ(1, pwrite(fd, &b, 1, at));
    close(fd);
}

static long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

TEST(test_round_trip) {
    rm_state();
    ASSERT_EQ(SETTINGS_LOAD_FRESH, boot(true));
    uint8_t v = 0;
    ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    settings_set_u8(SETTING_VOLUME_ATT_DB, 24);
    settings_set_u8(SETTING_BACKLIGHT_PCT, 55);
    settings_set_u8(SETTING_CLICK_FEEDBACK, 0);
    uint8_t key[27];
    for (unsigned i = 0; i < sizeof key; i++) {
        key[i] = (uint8_t)(i * 7u + 3u);
    }
    ASSERT_TRUE(settings_put(SETTINGS_TAG('B', 'T', 'L', 1), key, sizeof key));
    ASSERT_TRUE(settings_dirty());
    ASSERT_EQ(0, settings_flush());
    ASSERT_FALSE(settings_dirty());
    ASSERT_EQ(0, g_lock_depth); // every lock released
    ASSERT_EQ(-1, file_size(g_tmp)); // only file A exists after the first save
    ASSERT_TRUE(file_size(g_bin) > 0);
    ASSERT_FALSE(hal_storage_get_write_enabled()); // the card is locked again
    ASSERT_FALSE(nfs_write_scope_active());

    settings_status_t st;
    settings_get_status(&st);
    ASSERT_EQ(1, st.saves);
    ASSERT_EQ(4, st.items);

    // "Reboot": a new RAM store reads it back.
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(24, v);
    ASSERT_TRUE(settings_get_u8(SETTING_BACKLIGHT_PCT, &v));
    ASSERT_EQ(55, v);
    ASSERT_TRUE(settings_get_u8(SETTING_CLICK_FEEDBACK, &v));
    ASSERT_EQ(0, v);
    uint8_t back[48];
    ASSERT_EQ(27, settings_get(SETTINGS_TAG('B', 'T', 'L', 1), back, sizeof back));
    ASSERT_EQ(0, memcmp(back, key, sizeof key));
    ASSERT_FALSE(settings_dirty()); // nothing changed since the load
    ASSERT_EQ(0, settings_flush());  // and flushing a clean store writes nothing
    settings_get_status(&st);
    ASSERT_EQ(0, st.saves);
    ASSERT_EQ(1, st.seq);

    // Updating bumps the sequence; an identical put does not dirty the store.
    settings_set_u8(SETTING_BACKLIGHT_PCT, 55);
    ASSERT_FALSE(settings_dirty());
    settings_set_u8(SETTING_BACKLIGHT_PCT, 60);
    ASSERT_EQ(0, settings_flush());
    settings_get_status(&st);
    ASSERT_EQ(2, st.seq);
    // Delete.
    settings_delete(SETTINGS_TAG('B', 'T', 'L', 1));
    ASSERT_EQ(0, settings_flush());
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    ASSERT_EQ(0, settings_get(SETTINGS_TAG('B', 'T', 'L', 1), NULL, 0));
    ASSERT_TRUE(settings_get_u8(SETTING_BACKLIGHT_PCT, &v));
    ASSERT_EQ(60, v);
}

TEST(test_crc_and_format_failures_give_defaults) {
    // A good file, then damage it in each way.
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 20);
    ASSERT_EQ(0, settings_flush());
    long size = file_size(g_bin);
    ASSERT_TRUE(size > 16);

    file_corrupt_byte(g_bin, 14); // inside an item: CRC fails
    uint8_t v = 0;
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true));
    ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v)); // defaults
    ASSERT_FALSE(settings_dirty()); // a corrupt file alone does not trigger a rewrite
    settings_set_u8(SETTING_VOLUME_ATT_DB, 26); // the next change replaces it
    ASSERT_EQ(0, settings_flush());
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(26, v);

    file_corrupt_byte(g_bin, 0); // magic
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true));

    // Version field changed AND the CRC fixed up would still be refused; here only the CRC catches it, which is the
    // common case. Truncated file:
    ASSERT_EQ(0, truncate(g_bin, 7));
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true));
    ASSERT_EQ(0, truncate(g_bin, 0));
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true));

    // Oversized garbage is rejected without reading it into the buffer.
    int fd = open(g_bin, O_WRONLY | O_TRUNC);
    ASSERT_TRUE(fd >= 0);
    char junk[4000];
    memset(junk, 'x', sizeof junk);
    ASSERT_EQ((long)sizeof junk, write(fd, junk, sizeof junk));
    close(fd);
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true));
    ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
}

TEST(test_no_card_is_ram_only) {
    rm_state();
    // Never attached: setters work, nothing is written.
    settings_init(&k_lock);
    ASSERT_EQ(SETTINGS_LOAD_NONE, settings_load());
    settings_set_u8(SETTING_BACKLIGHT_PCT, 40);
    uint8_t v = 0;
    ASSERT_TRUE(settings_get_u8(SETTING_BACKLIGHT_PCT, &v));
    ASSERT_EQ(40, v);
    ASSERT_FALSE(settings_dirty()); // not attached: the storage task has nothing to do
    ASSERT_EQ(NFS_ERR_NOT_MOUNTED, settings_flush());
    ASSERT_EQ(-1, file_size(g_bin));
    ASSERT_EQ(-1, file_size(g_dir)); // not even the directory

    // Told there is no card (mount failed): same.
    settings_attach_storage(false);
    ASSERT_EQ(SETTINGS_LOAD_NONE, settings_load());
    settings_set_u8(SETTING_BACKLIGHT_PCT, 41);
    ASSERT_EQ(NFS_ERR_NOT_MOUNTED, settings_flush());
    ASSERT_EQ(-1, file_size(g_bin));
    settings_status_t st;
    settings_get_status(&st);
    ASSERT_FALSE(st.persistent);
    ASSERT_EQ(0, st.saves);

    // A card appears later (e.g. inserted): once loaded, the store saves normally.
    settings_attach_storage(true);
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load()); // load replaces RAM contents (documented: load once, first)
    settings_set_u8(SETTING_BACKLIGHT_PCT, 42);
    ASSERT_EQ(0, settings_flush());
    ASSERT_TRUE(file_size(g_bin) > 0);
}

TEST(test_power_loss_at_every_step) {
    uint8_t v;
    for (int step = 0; step <= 1; step++) {
        rm_state();
        boot(true);
        settings_set_u8(SETTING_VOLUME_ATT_DB, 30); // state A, saved properly (file A, seq 1)
        ASSERT_EQ(0, settings_flush());
        settings_set_u8(SETTING_VOLUME_ATT_DB, 18); // state B: written in place over file B, cut short
        settings_test_set_power_loss_step(step);
        ASSERT_EQ(NFS_ERR_IO, settings_flush());
        ASSERT_FALSE(hal_storage_get_write_enabled()); // even an aborted save restores the lock
        ASSERT_FALSE(nfs_write_scope_active());
        settings_test_set_power_loss_step(-1);

        settings_load_result_t r = boot(true); // power back on
        ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v)); // never lost entirely
        if (step == 0) { // half a file B: bad CRC, file A intact
            ASSERT_EQ(SETTINGS_LOAD_RECOVERED, r);
            ASSERT_EQ(30, v);
            // A second cut during the rewrite of the same inactive file, after the previous cut: still the old state.
            settings_set_u8(SETTING_VOLUME_ATT_DB, 12);
            settings_test_set_power_loss_step(0);
            ASSERT_EQ(NFS_ERR_IO, settings_flush());
            settings_test_set_power_loss_step(-1);
            ASSERT_EQ(SETTINGS_LOAD_RECOVERED, boot(true));
            ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
            ASSERT_EQ(30, v);
            // A normal save now completes, and the next boot sees it.
            settings_set_u8(SETTING_VOLUME_ATT_DB, 12);
            ASSERT_EQ(0, settings_flush());
            ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
            ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
            ASSERT_EQ(12, v);
        } else { // file B complete and synced, only the report was lost: the higher sequence wins
            ASSERT_EQ(SETTINGS_LOAD_OK, r);
            ASSERT_EQ(18, v);
        }
    }
}

TEST(test_alternation_never_touches_the_active_file) {
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 1);
    ASSERT_EQ(0, settings_flush()); // seq 1 -> A
    settings_set_u8(SETTING_VOLUME_ATT_DB, 2);
    ASSERT_EQ(0, settings_flush()); // seq 2 -> B
    long a = file_size(g_bin), b = file_size(g_tmp);
    ASSERT_TRUE(a > 0 && b > 0);
    // Damage the NEWER file (B): the load must fall back to A (seq 1) without having been rewritten.
    file_corrupt_byte(g_tmp, 13);
    uint8_t v = 0;
    ASSERT_EQ(SETTINGS_LOAD_RECOVERED, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(1, v);
    // The next save goes over the damaged B, not over A.
    settings_set_u8(SETTING_VOLUME_ATT_DB, 3);
    ASSERT_EQ(0, settings_flush());
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(3, v);
}

TEST(test_first_ever_save_power_loss) {
    // No previous file at all: a cut during the very first save leaves defaults, never garbage.
    for (int step = 0; step <= 1; step++) {
        rm_state();
        boot(true);
        settings_set_u8(SETTING_VOLUME_ATT_DB, 18);
        settings_test_set_power_loss_step(step);
        ASSERT_EQ(NFS_ERR_IO, settings_flush());
        settings_test_set_power_loss_step(-1);
        settings_load_result_t r = boot(true);
        uint8_t v = 0;
        if (step == 0) {
            ASSERT_EQ(SETTINGS_LOAD_CORRUPT, r); // a torn file and no other
            ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
        } else {
            ASSERT_EQ(SETTINGS_LOAD_OK, r);
            ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
            ASSERT_EQ(18, v);
        }
    }
}

TEST(test_winner_failing_its_reread_falls_back_to_the_other_file) {
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 10);
    ASSERT_EQ(0, settings_flush()); // file A, seq 1
    settings_set_u8(SETTING_VOLUME_ATT_DB, 20);
    ASSERT_EQ(0, settings_flush()); // file B, seq 2: the winner
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    uint8_t v = 0;
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(20, v);
    settings_test_fail_next_reread(); // the winner validated, but cannot be read the second time (card fault)
    ASSERT_EQ(SETTINGS_LOAD_IO_ERROR, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(10, v);                  // the older complete state, not a half-filled store
    settings_status_t st;
    settings_get_status(&st);
    ASSERT_EQ(1, st.seq);
    ASSERT_EQ(1, st.items);
    // with no other good file the failure is a card fault (I/O error, RAM-only), not "no settings"
    unlink(g_tmp);
    settings_test_fail_next_reread();
    ASSERT_EQ(SETTINGS_LOAD_IO_ERROR, boot(true));
    ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    // a version-1 file (the layout before the item limit was raised) is still read
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 33);
    ASSERT_EQ(0, settings_flush());
    int fd = open(g_bin, O_RDWR);
    unsigned char img[512];
    long n = pread(fd, img, sizeof img, 0);
    img[4] = 1;
    uint32_t c = 0xFFFFFFFFu;
    for (long i = 0; i < n - 4; i++) {
        c ^= img[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    c = ~c;
    for (int i = 0; i < 4; i++) img[n - 4 + i] = (unsigned char)(c >> (8 * i));
    ASSERT_EQ(n, pwrite(fd, img, (size_t)n, 0));
    close(fd);
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(33, v);
}

static long file_hash(const char *path) {
    FILE *f = fopen(path, "rb");
    long h = 17;
    int c;
    while (f != NULL && (c = fgetc(f)) != EOF) h = h * 31 + c;
    if (f != NULL) fclose(f);
    return f == NULL ? -1 : h;
}

TEST(test_io_error_on_the_winner_reread_goes_ram_only_and_never_writes) {
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 10);
    ASSERT_EQ(0, settings_flush()); // file A
    unlink(g_tmp);                  // only one good copy on the card
    long ha = file_hash(g_bin);
    ASSERT_TRUE(ha != -1);
    settings_init(&k_lock);
    settings_attach_storage(true);
    settings_test_ioerr_next_reread(); // it validated, then the card failed on the second read
    ASSERT_EQ(SETTINGS_LOAD_IO_ERROR, settings_load());
    settings_status_t st;
    settings_get_status(&st);
    ASSERT_FALSE(st.persistent);       // RAM-only
    settings_set_u8(SETTING_VOLUME_ATT_DB, 99);
    ASSERT_FALSE(settings_dirty());
    ASSERT_TRUE(settings_flush() != 0); // refuses to write
    ASSERT_EQ(ha, file_hash(g_bin));    // the only good copy is byte-identical
    ASSERT_EQ(-1, file_size(g_tmp));    // and no second file appeared
    // both files good, the newer one cannot be re-read: the older contents are used, but nothing may be written to either file
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 11);
    ASSERT_EQ(0, settings_flush());     // B (newer)
    settings_set_u8(SETTING_VOLUME_ATT_DB, 12);
    ASSERT_EQ(0, settings_flush());     // A (newest now); B holds 11
    long ha2 = file_hash(g_bin), hb2 = file_hash(g_tmp);
    settings_init(&k_lock);
    settings_attach_storage(true);
    settings_test_ioerr_next_reread();  // A wins, fails its re-read; B (11) is used
    ASSERT_EQ(SETTINGS_LOAD_IO_ERROR, settings_load());
    uint8_t v = 0;
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(11, v);                   // the device works with the older state
    settings_get_status(&st);
    ASSERT_FALSE(st.persistent);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 50);
    ASSERT_FALSE(settings_dirty());
    ASSERT_TRUE(settings_flush() != 0);
    ASSERT_EQ(ha2, file_hash(g_bin));   // both files byte-identical
    ASSERT_EQ(hb2, file_hash(g_tmp));
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
    ASSERT_EQ(50, v);                   // RAM still works
}

TEST(test_newer_version_file_is_kept) {
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 5);
    ASSERT_EQ(0, settings_flush());
    // Pretend a newer firmware wrote file A: bump the version field and fix the CRC.
    int fd = open(g_bin, O_RDWR);
    ASSERT_TRUE(fd >= 0);
    unsigned char img[512];
    long n = pread(fd, img, sizeof img, 0);
    ASSERT_TRUE(n > 16);
    img[4] = 9;
    uint32_t c = 0xFFFFFFFFu;
    for (long i = 0; i < n - 4; i++) {
        c ^= img[i];
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    c = ~c;
    for (int i = 0; i < 4; i++) {
        img[n - 4 + i] = (unsigned char)(c >> (8 * i));
    }
    ASSERT_EQ(n, pwrite(fd, img, (size_t)n, 0));
    close(fd);
    ASSERT_EQ(SETTINGS_LOAD_CORRUPT, boot(true)); // unreadable for us: defaults
    settings_set_u8(SETTING_VOLUME_ATT_DB, 6);
    ASSERT_EQ(0, settings_flush());
    char old[300];
    snprintf(old, sizeof old, "%s/settings.old", g_dir);
    ASSERT_TRUE(file_size(old) == n); // moved aside, not wiped
    unlink(old);
}

TEST(test_write_scope_confines_writes) {
    rm_state();
    ASSERT_FALSE(nfs_write_scope_active());
    ASSERT_TRUE(nfs_write_scope_allows("/anything")); // no scope: no restriction (the hardware lock still applies)
    ASSERT_EQ(NFS_ERR_INVALID, nfs_write_scope_enter("/")); // the whole volume is not a scope
    ASSERT_EQ(NFS_OK, nfs_write_scope_enter("/.neupod"));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_write_scope_enter("/.neupod")); // no nesting
    ASSERT_TRUE(nfs_write_scope_allows("/.neupod"));
    ASSERT_TRUE(nfs_write_scope_allows("/.neupod/settings.tmp"));
    ASSERT_TRUE(nfs_write_scope_allows("/.neupod/sub/../x"));
    ASSERT_FALSE(nfs_write_scope_allows("/"));
    ASSERT_FALSE(nfs_write_scope_allows("/x"));
    ASSERT_FALSE(nfs_write_scope_allows("/.neupod2/x")); // component-wise
    ASSERT_FALSE(nfs_write_scope_allows("/.neupod/../x")); // escapes via ..
    ASSERT_FALSE(nfs_write_scope_allows("relative/../../x"));
    ASSERT_FALSE(nfs_write_scope_allows(NULL));

    nfs_file_t *f;
    ASSERT_EQ(NFS_ERR_IO, nfs_open(&f, "/outside.txt", NFS_O_WRITE | NFS_O_CREATE));
    ASSERT_EQ(NFS_ERR_IO, nfs_mkdir("/other"));
    ASSERT_EQ(NFS_ERR_IO, nfs_unlink("/outside.txt"));
    ASSERT_EQ(NFS_OK, nfs_mkdir("/.neupod"));
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/.neupod/a", NFS_O_WRITE | NFS_O_CREATE));
    ASSERT_EQ(NFS_OK, nfs_close(f));
    ASSERT_EQ(NFS_ERR_IO, nfs_rename("/.neupod/a", "/escaped"));
    ASSERT_EQ(NFS_ERR_IO, nfs_rename("/outside", "/.neupod/b"));
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/.neupod/a", NFS_O_READ)); // reads are never restricted
    ASSERT_EQ(NFS_OK, nfs_close(f));
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_open(&f, "/nope", NFS_O_READ));
    ASSERT_EQ(NFS_OK, nfs_unlink("/.neupod/a"));
    nfs_write_scope_leave();
    ASSERT_FALSE(nfs_write_scope_active());
    ASSERT_TRUE(nfs_write_scope_allows("/x"));
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/outside.txt", NFS_O_WRITE | NFS_O_CREATE)); // outside the scope again
    ASSERT_EQ(NFS_OK, nfs_close(f));
    ASSERT_EQ(NFS_OK, nfs_unlink("/outside.txt"));
}

TEST(test_store_only_touches_its_directory) {
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 20);
    ASSERT_EQ(0, settings_flush());
    settings_set_u8(SETTING_VOLUME_ATT_DB, 22);
    ASSERT_EQ(0, settings_flush());
    // The test root holds exactly the one directory, with exactly settings.bin inside.
    DIR *d = opendir(g_root);
    ASSERT_TRUE(d != NULL);
    int entries = 0;
    for (struct dirent *e; d != NULL && (e = readdir(d)) != NULL;) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        entries++;
        ASSERT_EQ(0, strcmp(e->d_name, ".neupod"));
    }
    if (d != NULL) {
        closedir(d);
    }
    ASSERT_EQ(1, entries);
    d = opendir(g_dir);
    entries = 0;
    for (struct dirent *e; d != NULL && (e = readdir(d)) != NULL;) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) {
            entries++;
            ASSERT_TRUE(strcmp(e->d_name, "settings.a") == 0 || strcmp(e->d_name, "settings.b") == 0);
        }
    }
    if (d != NULL) {
        closedir(d);
    }
    ASSERT_EQ(2, entries); // the two alternating files and nothing else
}

TEST(test_hardware_lock_state_is_restored) {
    rm_state();
    boot(true);
    hal_storage_set_write_enabled(false);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 20);
    ASSERT_EQ(0, settings_flush());
    ASSERT_FALSE(hal_storage_get_write_enabled());
    // If a test menu had unlocked the card, the store leaves it unlocked (restores what it found).
    hal_storage_set_write_enabled(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 21);
    ASSERT_EQ(0, settings_flush());
    ASSERT_TRUE(hal_storage_get_write_enabled());
    hal_storage_set_write_enabled(false);
}

TEST(test_capacity_and_limits) {
    rm_state();
    boot(true);
    uint8_t v = 1;
    for (uint32_t i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        ASSERT_TRUE(settings_put(SETTINGS_TAG('T', 'S', 'T', (char)i), &v, 1));
    }
    ASSERT_FALSE(settings_put(SETTINGS_TAG('T', 'S', 'T', 99), &v, 1)); // full
    ASSERT_TRUE(settings_put(SETTINGS_TAG('T', 'S', 'T', 0), &v, 1));   // updating an existing tag still works
    settings_delete(SETTINGS_TAG('T', 'S', 'T', 3));
    ASSERT_TRUE(settings_put(SETTINGS_TAG('T', 'S', 'T', 99), &v, 1));  // a slot is free again
    uint8_t big[SETTINGS_VALUE_MAX + 1] = {0};
    ASSERT_FALSE(settings_put(SETTINGS_TAG('T', 'S', 'T', 0), big, sizeof big));
    ASSERT_FALSE(settings_put(SETTINGS_TAG('T', 'S', 'T', 0), big, 0));
    ASSERT_TRUE(settings_put(SETTINGS_TAG('T', 'S', 'T', 0), big, SETTINGS_VALUE_MAX));
    // A full store still saves and loads.
    ASSERT_EQ(0, settings_flush());
    ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
    settings_status_t st;
    settings_get_status(&st);
    ASSERT_EQ(SETTINGS_MAX_ITEMS, st.items);
    ASSERT_EQ(SETTINGS_VALUE_MAX, settings_get(SETTINGS_TAG('T', 'S', 'T', 0), NULL, 0));
}

TEST(test_bt_storage_adapter) {
    rm_state();
    const hal_bt_storage_t *bt = settings_bt_storage();
    uint8_t rec[27], got[8];
    memset(rec, 0xAB, sizeof rec);
    // Before the store knows about the card, writes are refused.
    settings_init(&k_lock);
    ASSERT_EQ(-1, bt->put(bt->ctx, 0x42544C00, rec, sizeof rec));
    // Card attached but not loaded yet: still refused (the load would drop it).
    settings_attach_storage(true);
    ASSERT_EQ(-1, bt->put(bt->ctx, 0x42544C00, rec, sizeof rec));
    ASSERT_EQ(SETTINGS_LOAD_FRESH, settings_load());
    ASSERT_EQ(0, bt->put(bt->ctx, 0x42544C00, rec, sizeof rec));
    ASSERT_EQ(27, bt->get(bt->ctx, 0x42544C00, NULL, 0));          // length only (the plaintext length: it is stored sealed)
    ASSERT_EQ(8, bt->get(bt->ctx, 0x42544C00, got, sizeof got));    // copies what fits
    ASSERT_EQ(0, bt->get(bt->ctx, 0x42544C01, got, sizeof got));    // absent
    ASSERT_EQ(-1, bt->put(bt->ctx, 0x42544C01, rec, 0));            // empty value is not storable
    bt->del(bt->ctx, 0x42544C00);
    ASSERT_EQ(0, bt->get(bt->ctx, 0x42544C00, got, sizeof got));
    // No card at all: usable for the session.
    settings_init(&k_lock);
    settings_attach_storage(false);
    ASSERT_EQ(0, bt->put(bt->ctx, 0x42544C00, rec, sizeof rec));
    ASSERT_EQ(27, bt->get(bt->ctx, 0x42544C00, NULL, 0));
    ASSERT_EQ(NFS_ERR_NOT_MOUNTED, settings_flush());
}

TEST(test_failing_card_falls_back_to_ram_only) {
    if (geteuid() == 0) {
        printf("  (skipped: running as root, directory permissions do not apply)\n");
        return;
    }
    rm_state();
    boot(true);
    settings_set_u8(SETTING_VOLUME_ATT_DB, 20);
    ASSERT_EQ(0, chmod(g_root, 0555)); // the "card" refuses new entries
    settings_status_t st;
    for (unsigned i = 0; i < SETTINGS_MAX_SAVE_FAILURES; i++) {
        ASSERT_TRUE(settings_flush() != 0);
        ASSERT_TRUE(settings_dirty() || i == SETTINGS_MAX_SAVE_FAILURES - 1);
    }
    settings_get_status(&st);
    ASSERT_EQ(SETTINGS_MAX_SAVE_FAILURES, st.save_failures);
    ASSERT_FALSE(st.persistent); // gave up: RAM-only
    ASSERT_EQ(NFS_ERR_NOT_MOUNTED, settings_flush());
    uint8_t v = 0;
    ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v)); // values still readable
    ASSERT_EQ(20, v);
    ASSERT_EQ(0, chmod(g_root, 0755));
    settings_attach_storage(true); // the card is back (re-attached by the caller)
    ASSERT_EQ(0, settings_flush());
    ASSERT_TRUE(file_size(g_bin) > 0);
    ASSERT_FALSE(hal_storage_get_write_enabled());
    ASSERT_FALSE(nfs_write_scope_active());
}

TEST(test_locks_are_balanced) {
    rm_state();
    g_lock_calls = 0;
    boot(true);
    settings_set_u8(SETTING_BACKLIGHT_PCT, 70);
    (void)settings_flush();
    uint8_t v;
    (void)settings_get_u8(SETTING_BACKLIGHT_PCT, &v);
    ASSERT_TRUE(g_lock_calls > 5);
    ASSERT_EQ(0, g_lock_depth);
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
    snprintf(g_dir, sizeof g_dir, "%s/.neupod", abs);
    snprintf(g_bin, sizeof g_bin, "%s/settings.a", g_dir);
    snprintf(g_tmp, sizeof g_tmp, "%s/settings.b", g_dir);
    snprintf(g_root, sizeof g_root, "%s", abs);

    RUN(test_round_trip);
    RUN(test_crc_and_format_failures_give_defaults);
    RUN(test_no_card_is_ram_only);
    RUN(test_power_loss_at_every_step);
    RUN(test_alternation_never_touches_the_active_file);
    RUN(test_first_ever_save_power_loss);
    RUN(test_winner_failing_its_reread_falls_back_to_the_other_file);
    RUN(test_io_error_on_the_winner_reread_goes_ram_only_and_never_writes);
    RUN(test_newer_version_file_is_kept);
    RUN(test_write_scope_confines_writes);
    RUN(test_store_only_touches_its_directory);
    RUN(test_hardware_lock_state_is_restored);
    RUN(test_capacity_and_limits);
    RUN(test_bt_storage_adapter);
    RUN(test_failing_card_falls_back_to_ram_only);
    RUN(test_locks_are_balanced);

    rm_state();
    rmdir(g_root);
    return RESULT();
}
