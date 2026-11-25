// SPDX-License-Identifier: MPL-2.0
// Y2 SD card selection against a fake sysfs/dev tree. The hard rule under test: mmcblk0 (the internal eMMC with the
// boot chain and the rootfs) and anything that is not an SD card on a non-mmc0 host is never picked.
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_sd.h"

static char g_sys[300], g_dev[300];

static void place_disk(const char *disk, const char *host);

static void tree(void) {
    fs_new();
    fs_path("sys/block", g_sys, sizeof g_sys);
    fs_path("dev", g_dev, sizeof g_dev);
    fs_mkdirs(g_dev);
    // The real Y2 with no card: the internal eMMC only, on host mmc0.
    place_disk("mmcblk0", "mmc0");
    fs_write("sys/block/mmcblk0/removable", "0\n");
    fs_write("sys/block/mmcblk0/device/type", "MMC\n");
    fs_write("sys/block/mmcblk0/dev", "179:0\n");
    fs_write("dev/mmcblk0", "");
    fs_write("dev/mmcblk0p7", "");
}

// Put /sys/block/<disk> where the kernel does: a symlink into .../mmc_host/<host>/<host>:0001/block/<disk>.
static void place_disk(const char *disk, const char *host) {
    char rel[200], dir[400], link[400], target[300];
    snprintf(rel, sizeof rel, "sys/devices/soc/mmc_host/%s/%s:0001/block/%s", host, host, disk);
    fs_path(rel, dir, sizeof dir);
    fs_mkdirs(dir);
    fs_path("sys/block", link, sizeof link);
    fs_mkdirs(link);
    snprintf(link + strlen(link), sizeof link - strlen(link), "/%s", disk);
    snprintf(target, sizeof target, "../devices/soc/mmc_host/%s/%s:0001/block/%s", host, host, disk);
    if (symlink(target, link) != 0) {
        perror("symlink");
        exit(2);
    }
    // The real layout: <disk>/device is a symlink up to the card (mmcX:RCA), where type, cid, serial ... live.
    snprintf(link + strlen(link), sizeof link - strlen(link), "/device");
    if (symlink("../..", link) != 0) {
        perror("symlink device");
        exit(2);
    }
}

static void insert_disk(const char *disk, const char *host, const char *removable, const char *type, unsigned minor) {
    char rel[200], val[32];
    place_disk(disk, host);
    snprintf(rel, sizeof rel, "sys/block/%s/removable", disk);
    fs_write(rel, removable);
    snprintf(rel, sizeof rel, "sys/block/%s/device/type", disk);
    fs_write(rel, type);
    snprintf(val, sizeof val, "179:%u\n", minor);
    snprintf(rel, sizeof rel, "sys/block/%s/dev", disk);
    fs_write(rel, val);
    snprintf(val, sizeof val, "179:%u\n", minor + 1);
    snprintf(rel, sizeof rel, "sys/block/%s/%sp1/dev", disk, disk);
    fs_write(rel, val);
    snprintf(rel, sizeof rel, "sys/block/%s/device/name", disk);
    fs_write(rel, "SD32G\n");
    snprintf(rel, sizeof rel, "sys/block/%s/device/serial", disk);
    fs_write(rel, "0x1234abcd\n");
    snprintf(rel, sizeof rel, "sys/block/%s/device/manfid", disk);
    fs_write(rel, "0x000003\n");
    snprintf(rel, sizeof rel, "sys/block/%s/device/date", disk);
    fs_write(rel, "05/2024\n");
}

// The real Y2: the SD slot is mmcblk1 on host mmc1 and the MTK driver reports removable=0.
static void insert_card(const char *removable, const char *type) { insert_disk("mmcblk1", "mmc1", removable, type, 8); }

TEST(name_rule) {
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1p1"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1p12"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk2"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk2p3"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk10"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk123p45"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0p1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0p7"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0boot0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0boot1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1boot0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk01"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk00"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk01p1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p01"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblkp1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk-1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk+1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk 1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1 "));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p1x"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p1p1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1x"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1234567890"));
    ASSERT_FALSE(y2_sd_name_allowed("MMCBLK1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1/../mmcblk0"));
    ASSERT_FALSE(y2_sd_name_allowed("/dev/mmcblk1"));
    ASSERT_FALSE(y2_sd_name_allowed("sda"));
    ASSERT_FALSE(y2_sd_name_allowed(""));
    ASSERT_FALSE(y2_sd_name_allowed(NULL));
}

TEST(no_card_means_absent_and_never_falls_back_to_the_emmc) {
    tree();
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_ABSENT, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "no card inserted") != NULL);
    fs_destroy();
}

TEST(card_found_prefers_whole_disk) {
    tree();
    insert_card("1\n", "SD\n");
    fs_write("dev/mmcblk1", "");
    fs_write("dev/mmcblk1p1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.name, "mmcblk1"));
    char want[400];
    snprintf(want, sizeof want, "%s/mmcblk1", g_dev);
    ASSERT_EQ(0, strcmp(p.node, want));
    fs_destroy();
}

TEST(partition_used_only_when_whole_disk_node_missing) {
    tree();
    insert_card("1\n", "SD\n");
    fs_write("dev/mmcblk1p1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.name, "mmcblk1p1"));
    fs_destroy();
}

TEST(sysfs_without_device_node_waits_for_mdev) {
    tree();
    insert_card("1\n", "SD\n");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_NO_NODE, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "no device node") != NULL);
    fs_destroy();
}

TEST(type_must_be_exactly_sd) {
    tree();
    insert_card("1\n", "MMC\n"); // on a non-mmc0 host, but not an SD card: refused
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "not type SD") != NULL);
    fs_write("sys/block/mmcblk1/device/type", "SDIO\n");
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_write("sys/block/mmcblk1/device/type", "sd\n");
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_write("sys/block/mmcblk1/device/type", "SD\n");
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(removable_zero_is_accepted_and_removable_is_ignored) {
    tree();
    insert_card("0\n", "SD\n"); // what the MTK driver reports on the real Y2
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.name, "mmcblk1"));
    char path[400];
    snprintf(path, sizeof path, "%s/mmcblk1/removable", g_sys);
    ASSERT_EQ(0, remove(path)); // absent attribute does not matter either
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(card_enumerating_as_mmcblk2_is_found) {
    tree();
    insert_disk("mmcblk2", "mmc2", "0\n", "SD\n", 16);
    fs_write("dev/mmcblk2", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.name, "mmcblk2"));
    ASSERT_EQ(0, strcmp(p.disk, "mmcblk2"));
    ASSERT_EQ(1, p.qualifying);
    ASSERT_TRUE(y2_sd_devnum_matches(g_sys, "mmcblk2", 179, 16));
    ASSERT_TRUE(y2_sd_devnum_matches(g_sys, "mmcblk2p1", 179, 17));
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk2", 179, 17));
    fs_destroy();
}

TEST(lowest_qualifying_disk_wins) {
    tree();
    insert_disk("mmcblk3", "mmc3", "0\n", "SD\n", 24);
    insert_disk("mmcblk2", "mmc2", "0\n", "SD\n", 16);
    insert_disk("mmcblk10", "mmc4", "0\n", "SD\n", 32);
    fs_write("dev/mmcblk2", "");
    fs_write("dev/mmcblk3", "");
    fs_write("dev/mmcblk10", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.disk, "mmcblk2")); // numeric order: 2 < 3 < 10
    ASSERT_EQ(3, p.qualifying);
    fs_destroy();
}

TEST(a_refused_lower_disk_does_not_hide_a_good_one) {
    tree();
    insert_disk("mmcblk1", "mmc1", "0\n", "MMC\n", 8); // not SD
    insert_disk("mmcblk2", "mmc2", "0\n", "SD\n", 16);
    fs_write("dev/mmcblk1", "");
    fs_write("dev/mmcblk2", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.disk, "mmcblk2"));
    ASSERT_EQ(1, p.qualifying);
    fs_destroy();
}

TEST(emmc_is_never_picked_by_name) {
    tree();
    fs_write("sys/block/mmcblk0/removable", "1\n"); // a lying sysfs
    fs_write("sys/block/mmcblk0/device/type", "SD\n");
    fs_write("sys/block/mmcblk0/mmcblk0p1/dev", "179:1\n");
    fs_write("sys/block/mmcblk0/mmcblk0boot0/dev", "179:32\n");
    fs_write("dev/mmcblk0boot0", "");
    fs_write("dev/mmcblk0p1", "");
    y2_sd_pick_t p;
    // mmcblk0* is never a candidate, whatever sysfs claims
    ASSERT_EQ(Y2_SD_ABSENT, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk0", 179, 0));
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk0p1", 179, 1));
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk0boot0", 179, 32));
    fs_destroy();
}

TEST(emmc_boot_partition_on_mmc0_is_refused) {
    tree();
    insert_disk("mmcblk0boot0", "mmc0", "1\n", "SD\n", 32); // a lying type on the eMMC's own host
    insert_disk("mmcblk0boot1", "mmc0", "1\n", "SD\n", 40);
    fs_write("dev/mmcblk0boot0", "");
    fs_write("dev/mmcblk0boot1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_ABSENT, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0boot0"));
    fs_destroy();
}

TEST(real_layout_device_is_a_symlink_to_the_card) {
    tree();
    insert_card("0\n", "SD\n");
    fs_write("dev/mmcblk1", "");
    char path[400], target[100];
    snprintf(path, sizeof path, "%s/mmcblk1/device", g_sys);
    ssize_t n = readlink(path, target, sizeof target - 1);
    ASSERT_TRUE(n > 0); // device really is a symlink, as in /sys
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    y2_sd_identity_t id;
    y2_sd_identity(g_sys, "mmcblk1", &id);
    ASSERT_EQ(0, strcmp(id.serial, "0x1234abcd"));
    fs_destroy();
}

TEST(nine_digit_names_work_through_pick_and_ten_do_not) {
    tree();
    insert_disk("mmcblk123456789", "mmc3", "0\n", "SD\n", 64);
    fs_write("dev/mmcblk123456789", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.disk, "mmcblk123456789"));
    ASSERT_TRUE(y2_sd_devnum_matches(g_sys, "mmcblk123456789p1", 179, 65));
    insert_disk("mmcblk1234567890", "mmc3", "0\n", "SD\n", 80); // 10 digits: not a candidate
    ASSERT_EQ(1, y2_sd_pick(g_sys, g_dev, &p, NULL, 0) == Y2_SD_FOUND && p.qualifying == 1);
    fs_destroy();
}

TEST(many_disks_do_not_break_lowest_wins) {
    tree();
    char name[32], node[40], host[16];
    for (int i = 5; i < 100; i++) { // far more than any fixed scan cap; only the highest few are SD
        snprintf(name, sizeof name, "mmcblk%d", i);
        snprintf(host, sizeof host, "mmc%d", i); // one card directory per host in the fake tree
        insert_disk(name, host, "0\n", i < 98 ? "MMC\n" : "SD\n", 8u * (unsigned)i);
        snprintf(node, sizeof node, "dev/%s", name);
        fs_write(node, "");
    }
    insert_disk("mmcblk4", "mmc4", "0\n", "SD\n", 32);
    fs_write("dev/mmcblk4", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    ASSERT_EQ(0, strcmp(p.disk, "mmcblk4"));
    ASSERT_EQ(3, p.qualifying); // 4, 98, 99
    fs_destroy();
}

static struct stat blk(unsigned maj, unsigned min) {
    struct stat st;
    memset(&st, 0, sizeof st);
    st.st_mode = S_IFBLK | 0660;
    st.st_rdev = makedev(maj, min);
    return st;
}

TEST(node_checks_before_open) {
    tree();
    insert_card("0\n", "SD\n"); // mmcblk1 is 179:8
    char why[96], node[400];
    struct stat st = blk(179, 8);
    ASSERT_TRUE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    // rdev mismatch
    st = blk(179, 16);
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    ASSERT_TRUE(strstr(why, "major:minor") != NULL);
    // rdev equal to mmcblk0's numbers (here also not what sysfs says)
    st = blk(179, 0);
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    // sysfs itself claiming mmcblk0's numbers for the card: refused even though rdev matches sysfs
    fs_write("sys/block/mmcblk1/dev", "179:0\n");
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    ASSERT_TRUE(strstr(why, "eMMC") != NULL);
    fs_write("sys/block/mmcblk1/dev", "179:3\n"); // inside the eMMC's minor range
    st = blk(179, 3);
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    // no mmcblk0/dev in sysfs: the 179:0..7 fallback still applies
    char p0[400];
    snprintf(p0, sizeof p0, "%s/mmcblk0/dev", g_sys);
    ASSERT_EQ(0, remove(p0));
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    fs_write("sys/block/mmcblk1/dev", "179:8\n");
    st = blk(179, 8);
    ASSERT_TRUE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    // not a block device / a symlink as seen by stat
    st = blk(179, 8);
    st.st_mode = S_IFREG | 0644;
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    ASSERT_TRUE(strstr(why, "not a block") != NULL);
    st.st_mode = S_IFLNK | 0777;
    ASSERT_FALSE(y2_sd_check_stat(g_sys, "mmcblk1", &st, why, sizeof why));
    ASSERT_TRUE(strstr(why, "symlink") != NULL);
    // the lstat wrapper: a regular file and a symlink in /dev are refused, a missing node and a bad name too
    fs_write("dev/mmcblk1", "");
    snprintf(node, sizeof node, "%s/mmcblk1", g_dev);
    ASSERT_FALSE(y2_sd_check_node(g_sys, node, "mmcblk1", why, sizeof why));
    ASSERT_TRUE(strstr(why, "not a block") != NULL);
    char link[400];
    snprintf(link, sizeof link, "%s/mmcblk1p1", g_dev);
    ASSERT_EQ(0, symlink("mmcblk0", link)); // a stale or hostile link pointing at the eMMC
    ASSERT_FALSE(y2_sd_check_node(g_sys, link, "mmcblk1p1", why, sizeof why));
    ASSERT_TRUE(strstr(why, "symlink") != NULL);
    snprintf(node, sizeof node, "%s/nothing", g_dev);
    ASSERT_FALSE(y2_sd_check_node(g_sys, node, "mmcblk1", why, sizeof why));
    ASSERT_FALSE(y2_sd_check_node(g_sys, node, "mmcblk0", why, sizeof why));
    fs_destroy();
}

TEST(held_disk_presence_and_reinsert) {
    tree();
    insert_card("0\n", "SD\n");
    fs_write("dev/mmcblk1", "");
    fs_write("sys/block/mmcblk1/device/cid", "1234567890abcdef\n");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    y2_sd_snapshot_t snap;
    y2_sd_snapshot(g_sys, p.disk, &snap);
    ASSERT_EQ(0, strcmp(snap.dev, "179:8"));
    ASSERT_EQ(Y2_SD_HELD_SAME, y2_sd_held_state(g_sys, g_dev, &p, &snap));

    // pulled and re-inserted within one poll: same node, a different card
    fs_write("sys/block/mmcblk1/device/serial", "0xdeadbeef\n");
    ASSERT_EQ(Y2_SD_HELD_CHANGED, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    fs_write("sys/block/mmcblk1/device/serial", "0x1234abcd\n");
    ASSERT_EQ(Y2_SD_HELD_SAME, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    // ... or the same serial but a new cid, or new device numbers (re-enumerated)
    fs_write("sys/block/mmcblk1/device/cid", "ffffffffffffffff\n");
    ASSERT_EQ(Y2_SD_HELD_CHANGED, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    fs_write("sys/block/mmcblk1/device/cid", "1234567890abcdef\n");
    fs_write("sys/block/mmcblk1/dev", "179:24\n");
    ASSERT_EQ(Y2_SD_HELD_CHANGED, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    fs_write("sys/block/mmcblk1/dev", "179:8\n");

    // the node vanished
    char path[400];
    snprintf(path, sizeof path, "%s/mmcblk1", g_dev);
    ASSERT_EQ(0, remove(path));
    ASSERT_EQ(Y2_SD_HELD_GONE, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    fs_write("dev/mmcblk1", "");
    ASSERT_EQ(Y2_SD_HELD_SAME, y2_sd_held_state(g_sys, g_dev, &p, &snap));

    // the held disk is gone while ANOTHER card is present under a new name: not "present"
    snprintf(path, sizeof path, "%s/mmcblk1", g_sys);
    ASSERT_EQ(0, remove(path));
    insert_disk("mmcblk2", "mmc1", "0\n", "SD\n", 16);
    fs_write("dev/mmcblk2", "");
    ASSERT_EQ(Y2_SD_HELD_GONE, y2_sd_held_state(g_sys, g_dev, &p, &snap));
    y2_sd_pick_t p2;
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p2, NULL, 0));
    ASSERT_EQ(0, strcmp(p2.disk, "mmcblk2")); // a fresh init would take it
    fs_destroy();
}

TEST(host_mmc0_is_refused_even_when_named_mmcblk1) {
    tree();
    insert_disk("mmcblk1", "mmc0", "1\n", "SD\n", 8);
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "mmc0") != NULL);
    fs_destroy();
}

TEST(host_that_cannot_be_determined_is_refused) {
    tree();
    // plain directory, not under any mmc_host: no way to tell the controller
    fs_write("sys/block/mmcblk1/removable", "0\n");
    fs_write("sys/block/mmcblk1/device/type", "SD\n");
    fs_write("sys/block/mmcblk1/dev", "179:8\n");
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "cannot determine") != NULL);
    fs_destroy();

    tree(); // a dangling link: unresolvable
    char link[400];
    fs_path("sys/block/mmcblk1", link, sizeof link);
    ASSERT_EQ(0, symlink("../devices/gone/mmc_host/mmc1/mmc1:0001/block/mmcblk1", link));
    fs_write("dev/mmcblk1", "");
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();

    tree(); // mmc_host component without a number
    place_disk("mmcblk1", "mmcx");
    fs_write("sys/block/mmcblk1/device/type", "SD\n");
    fs_write("dev/mmcblk1", "");
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(odd_names_are_never_candidates) {
    tree();
    insert_disk("mmcblk01", "mmc1", "0\n", "SD\n", 8);
    insert_disk("mmcblk1boot0", "mmc1", "0\n", "SD\n", 8);
    insert_disk("mmcblk1x", "mmc1", "0\n", "SD\n", 8);
    fs_write("dev/mmcblk01", "");
    fs_write("dev/mmcblk1boot0", "");
    fs_write("dev/mmcblk1x", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_ABSENT, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(missing_attributes_fail_closed) {
    tree();
    place_disk("mmcblk1", "mmc1"); // no device/type
    fs_write("sys/block/mmcblk1/dev", "179:8\n");
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(device_numbers_must_match_sysfs) {
    tree();
    insert_card("1\n", "SD\n");
    unsigned maj = 0, min = 0;
    ASSERT_TRUE(y2_sd_expected_devnum(g_sys, "mmcblk1", &maj, &min));
    ASSERT_EQ(179, maj);
    ASSERT_EQ(8, min);
    ASSERT_TRUE(y2_sd_expected_devnum(g_sys, "mmcblk1p1", &maj, &min));
    ASSERT_EQ(9, min);
    ASSERT_TRUE(y2_sd_devnum_matches(g_sys, "mmcblk1", 179, 8));
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk1", 8, 8)); // wrong major
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk1", 179, 0)); // a node that is really the eMMC
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk1p1", 179, 8));
    ASSERT_FALSE(y2_sd_expected_devnum(g_sys, "mmcblk0", &maj, &min)); // never even looked up
    ASSERT_FALSE(y2_sd_devnum_matches(g_sys, "mmcblk0", 179, 0));
    fs_destroy();
}

TEST(identity_strings) {
    tree();
    insert_card("1\n", "SD\n");
    y2_sd_identity_t id;
    y2_sd_identity(g_sys, "mmcblk1", &id);
    ASSERT_EQ(0, strcmp(id.name, "SD32G"));
    ASSERT_EQ(0, strcmp(id.serial, "0x1234abcd"));
    ASSERT_EQ(0, strcmp(id.date, "05/2024"));
    ASSERT_EQ(0, id.oemid[0]); // missing file stays empty
    fs_destroy();
}

int main(void) {
    RUN(name_rule);
    RUN(no_card_means_absent_and_never_falls_back_to_the_emmc);
    RUN(card_found_prefers_whole_disk);
    RUN(partition_used_only_when_whole_disk_node_missing);
    RUN(sysfs_without_device_node_waits_for_mdev);
    RUN(type_must_be_exactly_sd);
    RUN(removable_zero_is_accepted_and_removable_is_ignored);
    RUN(card_enumerating_as_mmcblk2_is_found);
    RUN(lowest_qualifying_disk_wins);
    RUN(a_refused_lower_disk_does_not_hide_a_good_one);
    RUN(emmc_is_never_picked_by_name);
    RUN(emmc_boot_partition_on_mmc0_is_refused);
    RUN(real_layout_device_is_a_symlink_to_the_card);
    RUN(nine_digit_names_work_through_pick_and_ten_do_not);
    RUN(many_disks_do_not_break_lowest_wins);
    RUN(node_checks_before_open);
    RUN(held_disk_presence_and_reinsert);
    RUN(host_mmc0_is_refused_even_when_named_mmcblk1);
    RUN(host_that_cannot_be_determined_is_refused);
    RUN(odd_names_are_never_candidates);
    RUN(missing_attributes_fail_closed);
    RUN(device_numbers_must_match_sysfs);
    RUN(identity_strings);
    return RESULT();
}
