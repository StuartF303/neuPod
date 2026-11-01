// SPDX-License-Identifier: MPL-2.0
/*
 * Host block storage backed by a disk-image file. Path: $NEUPOD_DISK_IMAGE, else
 * NEUPOD_DEFAULT_DISK_IMAGE (build/host/disk.img, set by CMake). The file is never created or
 * resized here; create it first (e.g. `truncate -s 64M build/host/disk.img`).
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "hal_storage.h"

#ifndef NEUPOD_DEFAULT_DISK_IMAGE
#define NEUPOD_DEFAULT_DISK_IMAGE "disk.img"
#endif

static FILE *g_img;
static uint32_t g_sectors;

hal_status_t hal_storage_init(void) {
    if (g_img != NULL) {
        fclose(g_img);
        g_img = NULL;
    }
    const char *path = getenv("NEUPOD_DISK_IMAGE");
    if (path == NULL || path[0] == '\0') {
        path = NEUPOD_DEFAULT_DISK_IMAGE;
    }
    g_img = fopen(path, "r+b");
    if (g_img == NULL) {
        fprintf(stderr, "neuPod host: cannot open disk image '%s'\n", path);
        return HAL_ENODEV;
    }
    if (fseeko(g_img, 0, SEEK_END) != 0) {
        fclose(g_img);
        g_img = NULL;
        return HAL_ERR;
    }
    off_t size = ftello(g_img);
    g_sectors = size > 0 ? (uint32_t)(size / HAL_STORAGE_SECTOR_SIZE) : 0;
    return g_sectors > 0 ? HAL_OK : HAL_ENODEV;
}

bool hal_storage_ready(void) { return g_img != NULL && g_sectors > 0; }

uint32_t hal_storage_sector_count(void) { return g_sectors; }

static bool in_range(uint32_t lba, uint32_t count) {
    return hal_storage_ready() && (uint64_t)lba + count <= g_sectors;
}

hal_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL || !in_range(lba, count)) {
        return HAL_EINVAL;
    }
    if (fseeko(g_img, (off_t)lba * HAL_STORAGE_SECTOR_SIZE, SEEK_SET) != 0) {
        return HAL_ERR;
    }
    size_t n = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    return fread(buf, 1, n, g_img) == n ? HAL_OK : HAL_ERR;
}

hal_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (buf == NULL || !in_range(lba, count)) {
        return HAL_EINVAL;
    }
    if (fseeko(g_img, (off_t)lba * HAL_STORAGE_SECTOR_SIZE, SEEK_SET) != 0) {
        return HAL_ERR;
    }
    size_t n = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    return fwrite(buf, 1, n, g_img) == n ? HAL_OK : HAL_ERR;
}

hal_status_t hal_storage_sync(void) { return fflush(g_img) == 0 ? HAL_OK : HAL_ERR; }
