// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 block storage: the microSD as a raw Linux block device (/dev/mmcblk1), read and written with pread/pwrite at
 * sector offsets under FatFs, the same layering as the Pico (diskio.c -> hal_storage). FatFs finds the partition
 * from the MBR (or mounts a card without one), exactly as on the Pico.
 *
 * SAFETY (y2_sd.h): this file can only ever open the device y2_sd_pick() returns, which is mmcblk1 or mmcblk1p<N>
 * of a disk that identifies as a removable SD card, and re-checks name and major:minor after open. mmcblk0 is the
 * internal eMMC with the boot chain and the rootfs; it is never opened. Fail closed: anything doubtful means "no card".
 *
 * Writes stay locked until hal_storage_set_write_enabled(true), as everywhere. The device is opened O_SYNC, so a
 * completed write is on the card; hal_storage_sync() adds an fsync.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include "ff.h"
#include "hal_storage.h"
#include "hal_storage_decode.h"
#include "y2_hal.h"
#include "y2_sd.h"

static int g_fd = -1;
static uint32_t g_sectors;
static bool g_write_enabled;
static bool g_write_fault;
static bool g_read_only_media;
static hal_card_info_t g_info;
static y2_sd_pick_t g_pick;

static FATFS g_fatfs;
static bool g_mounted;

static void close_device(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_sectors = 0;
}

static void fill_info(void) {
    memset(&g_info, 0, sizeof g_info);
    g_info.sectors = g_sectors;
    g_info.capacity_bytes = (uint64_t)g_sectors * HAL_STORAGE_SECTOR_SIZE;
    g_info.type = g_sectors > 67108864u ? HAL_CARD_SDXC : HAL_CARD_SDHC; // SDSC cannot be told apart from here
    g_info.csd_version = 1;
    g_info.write_protected = g_read_only_media;
    y2_sd_identity_t id;
    y2_sd_identity(Y2_SYS_BLOCK, g_pick.disk, &id);
    snprintf(g_info.pnm, sizeof g_info.pnm, "%.5s", id.name);
    g_info.mid = (uint8_t)strtoul(id.manfid, NULL, 0);
    g_info.psn = (uint32_t)strtoul(id.serial, NULL, 0);
    unsigned mo = 0, yr = 0;
    if (sscanf(id.date, "%u/%u", &mo, &yr) == 2) {
        g_info.mdt_month = (uint8_t)mo;
        g_info.mdt_year = (uint16_t)yr;
    }
}

hal_storage_status_t hal_storage_init(void) {
    hal_storage_unmount();
    close_device();
    g_write_fault = false;
    g_read_only_media = false;

    char why[96] = {0};
    switch (y2_sd_pick(Y2_SYS_BLOCK, Y2_DEV_DIR, &g_pick, why, sizeof why)) {
    case Y2_SD_FOUND: break;
    case Y2_SD_ABSENT:
    case Y2_SD_NO_NODE:
        printf("storage: %s\n", why);
        return HAL_STORAGE_NO_CARD;
    case Y2_SD_REFUSED:
        printf("storage: REFUSED: %s\n", why);
        return HAL_STORAGE_UNSUPPORTED;
    }
    // Last check before the open, independent of the picker: the name rule is the safety property.
    if (!y2_sd_name_allowed(g_pick.name)) {
        printf("storage: REFUSED: '%s' is not an allowed card device\n", g_pick.name);
        return HAL_STORAGE_UNSUPPORTED;
    }

    int flags = O_RDWR | O_SYNC | O_CLOEXEC;
    g_fd = open(g_pick.node, flags);
    if (g_fd < 0 && (errno == EACCES || errno == EROFS)) {
        g_fd = open(g_pick.node, O_RDONLY | O_CLOEXEC); // a write-protected card: readable, writes refused
        g_read_only_media = g_fd >= 0;
    }
    if (g_fd < 0) {
        printf("storage: cannot open %s: %s\n", g_pick.node, strerror(errno));
        return errno == ENOMEDIUM || errno == ENODEV || errno == ENXIO ? HAL_STORAGE_NO_CARD : HAL_STORAGE_IO;
    }
    struct stat st;
    if (fstat(g_fd, &st) != 0 || !S_ISBLK(st.st_mode)) {
        printf("storage: REFUSED: %s is not a block device\n", g_pick.node);
        close_device();
        return HAL_STORAGE_UNSUPPORTED;
    }
    if (!y2_sd_devnum_matches(Y2_SYS_BLOCK, g_pick.name, major(st.st_rdev), minor(st.st_rdev))) {
        printf("storage: REFUSED: %s is %u:%u, sysfs says that is not %s\n", g_pick.node, major(st.st_rdev), minor(st.st_rdev),
               g_pick.name);
        close_device();
        return HAL_STORAGE_UNSUPPORTED;
    }
    off_t size = lseek(g_fd, 0, SEEK_END);
    if (size < (off_t)HAL_STORAGE_SECTOR_SIZE) {
        printf("storage: %s has no medium\n", g_pick.node);
        close_device();
        return HAL_STORAGE_NO_CARD;
    }
    g_sectors = (uint32_t)(size / HAL_STORAGE_SECTOR_SIZE);
    fill_info();
    printf("storage: %s, %u sectors (%s)%s\n", g_pick.node, (unsigned)g_sectors, g_info.pnm[0] ? g_info.pnm : "card",
           g_read_only_media ? ", write-protected" : "");
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    hal_storage_unmount();
    close_device();
}

bool hal_storage_ready(void) { return g_fd >= 0 && g_sectors > 0; }

// Cheap sysfs look, silent: the hotplug poll calls this every couple of seconds while no card is in use.
bool hal_storage_card_present(void) {
    if (hal_storage_ready()) {
        return true;
    }
    y2_sd_pick_t p;
    return y2_sd_pick(Y2_SYS_BLOCK, Y2_DEV_DIR, &p, NULL, 0) == Y2_SD_FOUND;
}

uint32_t hal_storage_sector_count(void) { return g_sectors; }

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    *out = g_info;
    return HAL_STORAGE_OK;
}

static hal_storage_status_t io_error(int err) {
    if (err == ENOMEDIUM || err == ENODEV || err == ENXIO) {
        printf("storage: card went away (%s)\n", strerror(err));
        hal_storage_unmount();
        close_device();
        return HAL_STORAGE_NO_CARD;
    }
    return HAL_STORAGE_IO;
}

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if ((uint64_t)lba + count > g_sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    uint8_t *p = buf;
    size_t left = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    off_t off = (off_t)lba * HAL_STORAGE_SECTOR_SIZE;
    while (left > 0) {
        ssize_t r = pread(g_fd, p, left, off);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            return io_error(r < 0 ? errno : EIO);
        }
        p += r;
        off += r;
        left -= (size_t)r;
    }
    return HAL_STORAGE_OK;
}

void hal_storage_set_write_enabled(bool enabled) { g_write_enabled = enabled; }
bool hal_storage_get_write_enabled(void) { return g_write_enabled; }

hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (!g_write_enabled) {
        return HAL_STORAGE_WRITE_PROTECT; // checked first: nothing below may run while locked
    }
    if (g_write_fault) {
        return HAL_STORAGE_IO; // a failed write latches until hal_storage_init(), as on the Pico
    }
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_read_only_media) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    if ((uint64_t)lba + count > g_sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    const uint8_t *p = buf;
    size_t left = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
    off_t off = (off_t)lba * HAL_STORAGE_SECTOR_SIZE;
    while (left > 0) {
        ssize_t w = pwrite(g_fd, p, left, off);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            g_write_fault = true;
            return io_error(w < 0 ? errno : EIO);
        }
        p += w;
        off += w;
        left -= (size_t)w;
    }
    return HAL_STORAGE_OK;
}

hal_storage_status_t hal_storage_sync(void) {
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_read_only_media) {
        return HAL_STORAGE_OK;
    }
    return fsync(g_fd) == 0 ? HAL_STORAGE_OK : HAL_STORAGE_IO;
}

hal_storage_status_t hal_storage_enable_high_speed(void) { return HAL_STORAGE_UNSUPPORTED; }
hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz) {
    (void)hz;
    return HAL_STORAGE_UNSUPPORTED;
}
uint32_t hal_storage_get_bus_clock(void) { return 0; }

hal_storage_status_t hal_storage_card_status(uint32_t *r1) {
    if (r1 != NULL) {
        *r1 = 0x900; // transfer state, no errors: the kernel driver owns the bus
    }
    return hal_storage_ready() ? HAL_STORAGE_OK : HAL_STORAGE_NOT_INIT;
}

bool hal_storage_write_fault(void) { return g_write_fault; }

hal_storage_status_t hal_storage_mount(void) {
    if (!hal_storage_ready()) {
        return HAL_STORAGE_NOT_INIT;
    }
    switch (f_mount(&g_fatfs, "", 1)) { // reads only
    case FR_OK:
        g_mounted = true;
        return HAL_STORAGE_OK;
    case FR_NO_FILESYSTEM: return HAL_STORAGE_NO_FILESYSTEM;
    case FR_NOT_READY: return HAL_STORAGE_NO_CARD;
    default: return HAL_STORAGE_IO;
    }
}

void hal_storage_unmount(void) {
    if (g_mounted) {
        f_mount(NULL, "", 0);
        g_mounted = false;
    }
}

bool hal_storage_is_mounted(void) { return g_mounted && hal_storage_ready(); }

hal_fs_type_t hal_storage_fs_type(void) {
    if (!hal_storage_is_mounted()) {
        return HAL_FS_NONE;
    }
    switch (g_fatfs.fs_type) {
    case FS_FAT12: return HAL_FS_FAT12;
    case FS_FAT16: return HAL_FS_FAT16;
    case FS_FAT32: return HAL_FS_FAT32;
    case FS_EXFAT: return HAL_FS_EXFAT;
    default: return HAL_FS_NONE;
    }
}

hal_storage_status_t hal_storage_volume_info(hal_volume_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_is_mounted()) {
        return HAL_STORAGE_NOT_MOUNTED;
    }
    memset(out, 0, sizeof *out);
    out->fs = hal_storage_fs_type();
    out->sectors_per_cluster = g_fatfs.csize;
    out->cluster_bytes = out->sectors_per_cluster * HAL_STORAGE_SECTOR_SIZE;
    TCHAR label[36] = {0}; // FatFs needs at least 34 bytes
    DWORD vsn = 0;
    if (f_getlabel("", label, &vsn) != FR_OK) {
        return HAL_STORAGE_IO;
    }
    snprintf(out->label, sizeof out->label, "%s", label);
    out->serial = vsn;
    return HAL_STORAGE_OK;
}

// Formatting a card from the Y2 firmware is deliberately not offered: prepare the card on a PC.
hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts) {
    (void)label;
    (void)opts;
    return HAL_STORAGE_UNSUPPORTED;
}
