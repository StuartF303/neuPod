// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../app/storage/ff_alloc.h"
#include "ff.h"
#include "hal_storage.h"
#include "unit.h"

#define IMAGE_BYTES (8u * 1024u * 1024u)

static char g_path[] = "neupod_fatfs_test_XXXXXX";

static bool make_image(void) {
    int fd = mkstemp(g_path);
    if (fd < 0) {
        return false;
    }
    bool ok = ftruncate(fd, IMAGE_BYTES) == 0;
    close(fd);
    return ok && setenv("NEUPOD_DISK_IMAGE", g_path, 1) == 0;
}

TEST(test_ff_alloc_cap) {
    // FatFs asks for whole-cluster buffers and must be told NULL (not panic) when they are too big.
    void *ok = ff_memalloc(NEUPOD_FF_ALLOC_MAX_BYTES);
    ASSERT_TRUE(ok != NULL);
    ff_memfree(ok);
    ASSERT_TRUE(ff_memalloc(NEUPOD_FF_ALLOC_MAX_BYTES + 1u) == NULL);
    ASSERT_TRUE(ff_memalloc(32u * 1024u) == NULL);
    ASSERT_TRUE(ff_memalloc(128u * 1024u) == NULL);
    ASSERT_TRUE(ff_memalloc(0u) == NULL);
    ff_memfree(NULL);
}

TEST(test_fatfs_roundtrip) {
    static FATFS fs;
    static BYTE work[FF_MAX_SS * 4];
    ASSERT_TRUE(make_image());
    hal_storage_set_write_enabled(true); // writes are locked by default
    ASSERT_EQ(HAL_STORAGE_OK, hal_storage_init());
    ASSERT_EQ(IMAGE_BYTES / 512, hal_storage_sector_count());

    ASSERT_EQ(FR_OK, f_mkfs("", NULL, work, sizeof work));
    ASSERT_EQ(FR_OK, f_mount(&fs, "", 1));

    static const char msg[] = "hello from neuPod";
    FIL f;
    UINT n = 0;
    ASSERT_EQ(FR_OK, f_open(&f, "a long file name.txt", FA_CREATE_NEW | FA_WRITE));
    ASSERT_EQ(FR_OK, f_write(&f, msg, sizeof msg, &n));
    ASSERT_EQ(sizeof msg, n);
    ASSERT_EQ(FR_OK, f_close(&f));

    // Remount so the read comes from the image, not cached state.
    ASSERT_EQ(FR_OK, f_mount(NULL, "", 0));
    ASSERT_EQ(FR_OK, f_mount(&fs, "", 1));

    char buf[64] = {0};
    ASSERT_EQ(FR_OK, f_open(&f, "a long file name.txt", FA_READ));
    ASSERT_EQ(FR_OK, f_read(&f, buf, sizeof buf, &n));
    ASSERT_EQ(sizeof msg, n);
    ASSERT_EQ(0, memcmp(buf, msg, sizeof msg));
    ASSERT_EQ(FR_OK, f_close(&f));

    ASSERT_EQ(FR_OK, f_mount(NULL, "", 0));
    unlink(g_path);
}

int main(void) {
    RUN(test_ff_alloc_cap);
    RUN(test_fatfs_roundtrip);
    return RESULT();
}
