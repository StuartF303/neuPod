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
    ASSERT_EQ(-1, file_size(g_tmp)); // temp file renamed away
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
    for (int step = 0; step <= 3; step++) {
        rm_state();
        boot(true);
        settings_set_u8(SETTING_VOLUME_ATT_DB, 30); // state A, saved properly
        ASSERT_EQ(0, settings_flush());
        settings_set_u8(SETTING_VOLUME_ATT_DB, 18); // state B, the save is cut short
        settings_test_set_power_loss_step(step);
        ASSERT_EQ(NFS_ERR_IO, settings_flush());
        ASSERT_FALSE(hal_storage_get_write_enabled()); // even an aborted save restores the lock
        ASSERT_FALSE(nfs_write_scope_active());
        settings_test_set_power_loss_step(-1);

        settings_load_result_t r = boot(true); // power back on
        ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v)); // never lost entirely
        switch (step) {
        case 0: // half a temp file: invalid CRC, the old file is intact
            ASSERT_EQ(SETTINGS_LOAD_OK, r);
            ASSERT_EQ(30, v);
            break;
        case 1: // temp complete, old still there: the newer sequence wins
        case 2: // old deleted, rename not done: the temp is all there is
            ASSERT_EQ(SETTINGS_LOAD_RECOVERED, r);
            ASSERT_EQ(18, v);
            ASSERT_TRUE(settings_dirty()); // it is promoted to settings.bin by the next flush
            ASSERT_EQ(0, settings_flush());
            ASSERT_EQ(-1, file_size(g_tmp));
            ASSERT_EQ(SETTINGS_LOAD_OK, boot(true));
            ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
            ASSERT_EQ(18, v);
            break;
        case 3: // done, only the caller's report was lost
            ASSERT_EQ(SETTINGS_LOAD_OK, r);
            ASSERT_EQ(18, v);
            break;
        }
    }
}

TEST(test_first_ever_save_power_loss) {
    // No previous file at all: a cut during the very first save leaves defaults, never garbage.
    for (int step = 0; step <= 2; step++) {
        rm_state();
        boot(true);
        settings_set_u8(SETTING_VOLUME_ATT_DB, 18);
        settings_test_set_power_loss_step(step);
        ASSERT_EQ(NFS_ERR_IO, settings_flush());
        settings_test_set_power_loss_step(-1);
        settings_load_result_t r = boot(true);
        uint8_t v = 0;
        if (step == 0) {
            ASSERT_EQ(SETTINGS_LOAD_CORRUPT, r); // a torn temp file and no old file
            ASSERT_FALSE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
        } else {
            ASSERT_EQ(SETTINGS_LOAD_RECOVERED, r);
            ASSERT_TRUE(settings_get_u8(SETTING_VOLUME_ATT_DB, &v));
            ASSERT_EQ(18, v);
        }
    }
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
            ASSERT_EQ(0, strcmp(e->d_name, "settings.bin"));
        }
    }
    if (d != NULL) {
        closedir(d);
    }
    ASSERT_EQ(1, entries);
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
    snprintf(g_bin, sizeof g_bin, "%s/settings.bin", g_dir);
    snprintf(g_tmp, sizeof g_tmp, "%s/settings.tmp", g_dir);
    snprintf(g_root, sizeof g_root, "%s", abs);

    RUN(test_round_trip);
    RUN(test_crc_and_format_failures_give_defaults);
    RUN(test_no_card_is_ram_only);
    RUN(test_power_loss_at_every_step);
    RUN(test_first_ever_save_power_loss);
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
