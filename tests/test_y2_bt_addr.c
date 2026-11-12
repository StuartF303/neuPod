// SPDX-License-Identifier: MPL-2.0
// The Y2's Bluetooth address file: read only, 6 bytes MSB first, refuses missing / short / all-zero / all-0xFF. Test addresses only.
#include "unit.h"
#include "y2_bt_addr.h"
#include "y2_fake_fs.h"

static void put_bytes(const char *rel, const uint8_t *b, size_t n) {
    fs_write(rel, "");
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_root, rel);
    FILE *f = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite(b, 1, n, f);
    fclose(f);
}

static char *path_of(const char *rel, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", g_root, rel);
    return out;
}

TEST(reads_six_bytes_msb_first) {
    fs_new();
    const uint8_t file[8] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0xAA, 0xBB}; // extra bytes after the address are ignored
    put_bytes("data/BT_Addr", file, sizeof file);
    char p[512];
    uint8_t a[6] = {0};
    ASSERT_EQ(Y2_BT_ADDR_OK, y2_bt_addr_read(path_of("data/BT_Addr", p, sizeof p), a));
    ASSERT_TRUE(memcmp(a, file, 6) == 0);
    fs_destroy();
}

TEST(missing_file_is_refused) {
    fs_new();
    char p[512];
    uint8_t a[6] = {9, 9, 9, 9, 9, 9};
    ASSERT_EQ(Y2_BT_ADDR_UNREADABLE, y2_bt_addr_read(path_of("nope", p, sizeof p), a));
    ASSERT_EQ(9, a[0]); // untouched
    fs_destroy();
}

TEST(short_file_is_refused) {
    fs_new();
    const uint8_t file[5] = {1, 2, 3, 4, 5};
    put_bytes("short", file, sizeof file);
    char p[512];
    uint8_t a[6] = {0};
    ASSERT_EQ(Y2_BT_ADDR_SHORT, y2_bt_addr_read(path_of("short", p, sizeof p), a));
    fs_destroy();
}

TEST(all_zero_and_all_ff_are_refused) {
    fs_new();
    const uint8_t zero[6] = {0};
    const uint8_t ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    put_bytes("zero", zero, sizeof zero);
    put_bytes("ones", ones, sizeof ones);
    char p[512];
    uint8_t a[6] = {7, 7, 7, 7, 7, 7};
    ASSERT_EQ(Y2_BT_ADDR_INVALID, y2_bt_addr_read(path_of("zero", p, sizeof p), a));
    ASSERT_EQ(Y2_BT_ADDR_INVALID, y2_bt_addr_read(path_of("ones", p, sizeof p), a));
    ASSERT_EQ(7, a[0]);
    fs_destroy();
}

TEST(file_is_opened_read_only) {
    fs_new();
    const uint8_t file[6] = {2, 4, 6, 8, 10, 12};
    put_bytes("ro", file, sizeof file);
    char p[512];
    path_of("ro", p, sizeof p);
    ASSERT_EQ(0, chmod(p, 0444)); // a read-only file must still be readable: the reader never asks for write access
    uint8_t a[6];
    ASSERT_EQ(Y2_BT_ADDR_OK, y2_bt_addr_read(p, a));
    fs_destroy();
}

int main(void) {
    RUN(reads_six_bytes_msb_first);
    RUN(missing_file_is_refused);
    RUN(short_file_is_refused);
    RUN(all_zero_and_all_ff_are_refused);
    RUN(file_is_opened_read_only);
    return RESULT();
}
