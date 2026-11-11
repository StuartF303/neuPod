// SPDX-License-Identifier: MPL-2.0
// Y2 SD card selection against a fake sysfs/dev tree. The hard rule under test: mmcblk0 (the internal eMMC with the
// boot chain and the rootfs) and anything that is not a removable SD card is never picked.
#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_sd.h"

static char g_sys[300], g_dev[300];

static void tree(void) {
    fs_new();
    fs_path("sys/block", g_sys, sizeof g_sys);
    fs_path("dev", g_dev, sizeof g_dev);
    fs_mkdirs(g_dev);
    // The real Y2 with no card: the internal eMMC only.
    fs_write("sys/block/mmcblk0/removable", "0\n");
    fs_write("sys/block/mmcblk0/device/type", "MMC\n");
    fs_write("sys/block/mmcblk0/dev", "179:0\n");
    fs_write("dev/mmcblk0", "");
    fs_write("dev/mmcblk0p7", "");
}

static void insert_card(const char *removable, const char *type) {
    fs_write("sys/block/mmcblk1/removable", removable);
    fs_write("sys/block/mmcblk1/device/type", type);
    fs_write("sys/block/mmcblk1/dev", "179:8\n");
    fs_write("sys/block/mmcblk1/mmcblk1p1/dev", "179:9\n");
    fs_write("sys/block/mmcblk1/device/name", "SD32G\n");
    fs_write("sys/block/mmcblk1/device/serial", "0x1234abcd\n");
    fs_write("sys/block/mmcblk1/device/manfid", "0x000003\n");
    fs_write("sys/block/mmcblk1/device/date", "05/2024\n");
}

TEST(name_rule) {
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1p1"));
    ASSERT_TRUE(y2_sd_name_allowed("mmcblk1p12"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0p1"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0p7"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk0boot0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1boot0"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk10"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p"));
    ASSERT_FALSE(y2_sd_name_allowed("mmcblk1p1x"));
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
    ASSERT_TRUE(strstr(why, "no mmcblk1") != NULL);
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

TEST(type_sd_is_required_removable_alone_is_not_enough) {
    tree();
    insert_card("1\n", "MMC\n"); // removable, but not an SD card: refused
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "not type SD") != NULL);
    insert_card("1\n", "SD\n");
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(removable_only_confirms_a_contradiction_refuses) {
    tree();
    insert_card("0\n", "SD\n"); // says SD but not removable: contradictory, refused
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "not removable") != NULL);
    // an absent removable attribute does not matter when the type says SD
    char path[400];
    snprintf(path, sizeof path, "%s/mmcblk1/removable", g_sys);
    ASSERT_EQ(0, remove(path));
    ASSERT_EQ(Y2_SD_FOUND, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(a_fixed_non_sd_mmcblk1_is_refused) {
    tree();
    insert_card("0\n", "MMC\n"); // e.g. a board where mmcblk1 is another eMMC
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    char why[96];
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, why, sizeof why));
    ASSERT_TRUE(strstr(why, "refused") != NULL);
    fs_destroy();
}

TEST(missing_attributes_fail_closed) {
    tree();
    fs_write("sys/block/mmcblk1/dev", "179:8\n"); // no device/type
    fs_write("dev/mmcblk1", "");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_REFUSED, y2_sd_pick(g_sys, g_dev, &p, NULL, 0));
    fs_destroy();
}

TEST(emmc_is_never_picked_even_when_it_looks_removable) {
    tree();
    fs_write("sys/block/mmcblk0/removable", "1\n"); // a lying sysfs
    fs_write("sys/block/mmcblk0/device/type", "SD\n");
    y2_sd_pick_t p;
    ASSERT_EQ(Y2_SD_ABSENT, y2_sd_pick(g_sys, g_dev, &p, NULL, 0)); // only mmcblk1 is ever looked at
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
    RUN(type_sd_is_required_removable_alone_is_not_enough);
    RUN(removable_only_confirms_a_contradiction_refuses);
    RUN(a_fixed_non_sd_mmcblk1_is_refused);
    RUN(missing_attributes_fail_closed);
    RUN(emmc_is_never_picked_even_when_it_looks_removable);
    RUN(device_numbers_must_match_sysfs);
    RUN(identity_strings);
    return RESULT();
}
