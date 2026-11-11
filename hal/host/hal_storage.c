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

#include <string.h>

#include "hal_storage.h"
#include "hal_storage_decode.h"

#ifndef NEUPOD_DEFAULT_DISK_IMAGE
#define NEUPOD_DEFAULT_DISK_IMAGE "disk.img"
#endif

static FILE *g_img;
static uint32_t g_sectors;
static bool g_write_enabled;

hal_storage_status_t hal_storage_init(void) {
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
        g_img = fopen(path, "rb"); // read-only image is fine; writes will then fail
    }
    if (g_img == NULL) {
        fprintf(stderr, "neuPod host: cannot open disk image '%s'\n", path);
        return HAL_STORAGE_NO_CARD;
    }
    if (fseeko(g_img, 0, SEEK_END) != 0) {
        fclose(g_img);
        g_img = NULL;
        return HAL_STORAGE_IO;
    }
    off_t size = ftello(g_img);
    g_sectors = size > 0 ? (uint32_t)(size / HAL_STORAGE_SECTOR_SIZE) : 0;
    if (g_sectors == 0) {
        fclose(g_img);
        g_img = NULL;
        return HAL_STORAGE_NO_CARD;
    }
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    if (g_img != NULL) {
        fclose(g_img);
        g_img = NULL;
    }
    g_sectors = 0;
}

bool hal_storage_ready(void) { return g_img != NULL && g_sectors > 0; }

bool hal_storage_card_present(void) { return hal_storage_ready(); }

uint32_t hal_storage_sector_count(void) { return g_sectors; }

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    memset(out, 0, sizeof *out);
    out->type = HAL_CARD_IMAGE;
    out->sectors = g_sectors;
    out->capacity_bytes = (uint64_t)g_sectors * HAL_STORAGE_SECTOR_SIZE;
    out->mid = 0x00; // synthetic identity
    memcpy(out->oid, "HO", 3);
    memcpy(out->pnm, "IMAGE", 6);
    out->prv_major = 1;
    out->psn = 0xC0FFEE00u;
    out->mdt_year = 2026;
    out->mdt_month = 1;
    out->csd_version = 1;
    out->bus_width = 1;
    return HAL_STORAGE_OK;
}

static bool in_range(uint32_t lba, uint32_t count) {
    return hal_storage_ready() && (uint64_t)lba + count <= g_sectors;
}

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL || !hal_storage_ready()) {
        return buf == NULL ? HAL_STORAGE_BAD_PARAM : HAL_STORAGE_NOT_INIT;
    }
    if (!in_range(lba, count)) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (fseeko(g_img, (off_t)lba * HAL_STORAGE_SECTOR_SIZE, SEEK_SET) != 0) {
        return HAL_STORAGE_IO;
    }
    size_t n = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    return fread(buf, 1, n, g_img) == n ? HAL_STORAGE_OK : HAL_STORAGE_IO;
}

void hal_storage_set_write_enabled(bool enabled) { g_write_enabled = enabled; }
bool hal_storage_get_write_enabled(void) { return g_write_enabled; }

hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (!g_write_enabled) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (!in_range(lba, count)) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (fseeko(g_img, (off_t)lba * HAL_STORAGE_SECTOR_SIZE, SEEK_SET) != 0) {
        return HAL_STORAGE_IO;
    }
    size_t n = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    return fwrite(buf, 1, n, g_img) == n ? HAL_STORAGE_OK : HAL_STORAGE_IO;
}

hal_storage_status_t hal_storage_sync(void) {
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    return fflush(g_img) == 0 ? HAL_STORAGE_OK : HAL_STORAGE_IO;
}

hal_storage_status_t hal_storage_enable_high_speed(void) { return HAL_STORAGE_UNSUPPORTED; }

hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz) {
    (void)hz;
    return HAL_STORAGE_UNSUPPORTED;
}
uint32_t hal_storage_get_bus_clock(void) { return 0; }

hal_storage_status_t hal_storage_volume_info(hal_volume_info_t *out) {
    (void)out;
    return HAL_STORAGE_UNSUPPORTED;
}

hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts) {
    (void)label;
    (void)opts;
    return HAL_STORAGE_UNSUPPORTED;
}

hal_storage_status_t hal_storage_card_status(uint32_t *r1) {
    if (r1 != NULL) {
        *r1 = 0x900; // tran state, no errors
    }
    return hal_storage_ready() ? HAL_STORAGE_OK : HAL_STORAGE_NOT_INIT;
}

bool hal_storage_write_fault(void) { return false; }

hal_storage_status_t hal_storage_mount(void) { return HAL_STORAGE_UNSUPPORTED; }
void hal_storage_unmount(void) {}
static bool g_test_mounted;
bool hal_storage_is_mounted(void) { return g_test_mounted; }
void hal_host_storage_set_mounted(bool mounted) { g_test_mounted = mounted; }
hal_fs_type_t hal_storage_fs_type(void) { return HAL_FS_NONE; }
