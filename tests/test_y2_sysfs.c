// SPDX-License-Identifier: MPL-2.0
// y2_sysfs_read_line: the EINTR-safe small-file reader behind every sysfs lookup.
#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_sysfs.h"

TEST(reads_first_line_and_strips_newline) {
    fs_new();
    fs_write("a", "SD\n");
    fs_write("b", "line one\r\nline two\n");
    fs_write("c", "no newline");
    char p[400], v[32];
    ASSERT_TRUE(y2_sysfs_read_line(fs_path("a", p, sizeof p), v, sizeof v));
    ASSERT_EQ(0, strcmp(v, "SD"));
    ASSERT_TRUE(y2_sysfs_read_line(fs_path("b", p, sizeof p), v, sizeof v));
    ASSERT_EQ(0, strcmp(v, "line one"));
    ASSERT_TRUE(y2_sysfs_read_line(fs_path("c", p, sizeof p), v, sizeof v));
    ASSERT_EQ(0, strcmp(v, "no newline"));
    fs_destroy();
}

TEST(truncates_to_the_buffer_and_fails_cleanly) {
    fs_new();
    fs_write("long", "0123456789abcdef\n");
    fs_write("empty", "");
    char p[400], v[8];
    ASSERT_TRUE(y2_sysfs_read_line(fs_path("long", p, sizeof p), v, sizeof v));
    ASSERT_EQ(0, strcmp(v, "0123456")); // cap - 1 bytes, still terminated
    ASSERT_FALSE(y2_sysfs_read_line(fs_path("empty", p, sizeof p), v, sizeof v));
    ASSERT_EQ(0, v[0]);
    ASSERT_FALSE(y2_sysfs_read_line("/nonexistent/file", v, sizeof v));
    ASSERT_FALSE(y2_sysfs_read_line(fs_path("long", p, sizeof p), v, 0));
    fs_destroy();
}

TEST(boot_mode_detection) {
    fs_new();
    char p[400];
    fs_write("m8", "8\n");
    fs_write("m9", "9");
    fs_write("m0", "0\n");
    fs_write("junk", "charger\n");
    fs_write("empty", "");
    ASSERT_EQ(8, y2_boot_mode_read(fs_path("m8", p, sizeof p)));
    ASSERT_TRUE(y2_boot_mode_is_charging(8));
    ASSERT_EQ(9, y2_boot_mode_read(fs_path("m9", p, sizeof p)));
    ASSERT_TRUE(y2_boot_mode_is_charging(9));
    ASSERT_EQ(0, y2_boot_mode_read(fs_path("m0", p, sizeof p)));
    ASSERT_FALSE(y2_boot_mode_is_charging(0));
    ASSERT_FALSE(y2_boot_mode_is_charging(1)); // other modes (factory, recovery ...) are not charging
    ASSERT_FALSE(y2_boot_mode_is_charging(7));
    ASSERT_FALSE(y2_boot_mode_is_charging(10));
    // absent, empty or not a number: -1, which is a normal boot
    ASSERT_EQ(-1, y2_boot_mode_read(fs_path("junk", p, sizeof p)));
    ASSERT_EQ(-1, y2_boot_mode_read(fs_path("empty", p, sizeof p)));
    ASSERT_EQ(-1, y2_boot_mode_read("/nonexistent/boot_mode"));
    ASSERT_FALSE(y2_boot_mode_is_charging(-1));
    fs_destroy();
}

int main(void) {
    RUN(reads_first_line_and_strips_newline);
    RUN(truncates_to_the_buffer_and_fails_cleanly);
    RUN(boot_mode_detection);
    return RESULT();
}
