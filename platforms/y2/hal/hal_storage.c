// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 block storage: the microSD as a raw Linux block device (/dev/mmcblk1), read and written with pread/pwrite at
 * sector offsets under FatFs, the same layering as the Pico (diskio.c -> hal_storage). FatFs finds the partition
 * from the MBR (or mounts a card without one), exactly as on the Pico.
 *
 * SAFETY (y2_sd.h): this file can only ever open the device y2_sd_pick() returns, which is mmcblk1 or mmcblk1p<N>
 * of a disk whose sysfs type is SD, and every open re-checks the sysfs identity, the name and the node's major:minor.
 * mmcblk0 is the internal eMMC with the boot chain and the rootfs; it is never opened. Fail closed: anything
 * doubtful means "no card".
 *
 * Read-only by default: the device is opened O_RDONLY while writes are locked. hal_storage_set_write_enabled(true)
 * reopens it O_RDWR | O_SYNC (so a completed write is on the card) after validating it again; set_write_enabled(false)
 * fsyncs and goes back to O_RDONLY.
 *
 * Locking: g_fd and its flags are protected by one lock. It is taken with try-lock plus a scheduler sleep, never a blocking
 * pthread lock: under the FreeRTOS POSIX port a higher-priority task blocked natively on a lock held by a preempted
 * lower-priority task would never let that task run again. No FatFs call is made while the lock is held.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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
#include "hal_system.h"
#include "y2_hal.h"
#include "y2_sd.h"

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_fd = -1;
static bool g_fd_rw;
static uint32_t g_sectors;
static bool g_write_enabled;
static bool g_write_fault;
static bool g_read_only_media; // the card refused a read-write open (write-protect switch)
static hal_card_info_t g_info;
static y2_sd_pick_t g_pick;

static FATFS g_fatfs;
static bool g_mounted;

static void lock(void) {
    while (pthread_mutex_trylock(&g_mu) != 0) {
        hal_delay_ms(1); // lets the holder (maybe a lower-priority task) run
    }
}

static void unlock(void) { pthread_mutex_unlock(&g_mu); }

// Lock held.
static void close_device(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_fd_rw = false;
    g_sectors = 0;
}

// Open the picked card device, read-only or read-write, and validate it: the sysfs identity is checked again (the same
// disk must still be the card), the name rule once more, and the opened node must be a block device whose major:minor is
// what sysfs says. Returns the fd, or -1 with errno-style reason in *why_errno (0 = refused by validation).
// Lock held.
static int open_validated(bool rw, y2_sd_pick_t *pick_out, int *why_errno) {
    char why[96] = {0};
    y2_sd_pick_t pick;
    *why_errno = 0;
    switch (y2_sd_pick(Y2_SYS_BLOCK, Y2_DEV_DIR, &pick, why, sizeof why)) {
    case Y2_SD_FOUND: break;
    case Y2_SD_ABSENT:
    case Y2_SD_NO_NODE:
        printf("storage: %s\n", why);
        *why_errno = ENODEV;
        return -1;
    case Y2_SD_REFUSED:
        printf("storage: REFUSED: %s\n", why);
        return -1;
    }
    // Last check before the open, independent of the picker: the name rule is the safety property.
    if (!y2_sd_name_allowed(pick.name)) {
        printf("storage: REFUSED: '%s' is not an allowed card device\n", pick.name);
        return -1;
    }
    int fd = open(pick.node, (rw ? (O_RDWR | O_SYNC) : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) {
        *why_errno = errno;
        if (!(rw && (errno == EACCES || errno == EROFS))) {
            printf("storage: cannot open %s: %s\n", pick.node, strerror(errno));
        }
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISBLK(st.st_mode)) {
        printf("storage: REFUSED: %s is not a block device\n", pick.node);
        close(fd);
        return -1;
    }
    if (!y2_sd_devnum_matches(Y2_SYS_BLOCK, pick.name, major(st.st_rdev), minor(st.st_rdev))) {
        printf("storage: REFUSED: %s is %u:%u, sysfs says that is not %s\n", pick.node, major(st.st_rdev), minor(st.st_rdev),
               pick.name);
        close(fd);
        return -1;
    }
    if (pick_out != NULL) {
        *pick_out = pick;
    }
    return fd;
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

// Lock held. Switch the open device to read-write (or back to read-only) by opening a validated replacement first.
static bool reopen(bool rw) {
    if (g_fd < 0 || g_fd_rw == rw) {
        return g_fd >= 0;
    }
    if (!rw) {
        (void)fsync(g_fd); // everything written while unlocked is on the card before it goes back to read-only
    }
    int why;
    y2_sd_pick_t pick;
    int fd = open_validated(rw, &pick, &why);
    if (fd < 0) {
        if (rw && (why == EACCES || why == EROFS)) {
            g_read_only_media = true;
            g_info.write_protected = true;
        }
        return false;
    }
    if (strcmp(pick.name, g_pick.name) != 0) { // the card moved to a different node: not the device we were using
        printf("storage: %s is now %s, not reopening\n", g_pick.name, pick.name);
        close(fd);
        return false;
    }
    close(g_fd);
    g_fd = fd;
    g_fd_rw = rw;
    return true;
}

hal_storage_status_t hal_storage_init(void) {
    hal_storage_unmount(); // FatFs, outside the lock
    lock();
    close_device();
    g_write_fault = false;
    g_read_only_media = false;
    int why;
    int fd = open_validated(false, &g_pick, &why);
    if (fd < 0) {
        unlock();
        return why == ENODEV || why == ENOMEDIUM || why == ENXIO ? HAL_STORAGE_NO_CARD : why == 0 ? HAL_STORAGE_UNSUPPORTED : HAL_STORAGE_IO;
    }
    off_t size = lseek(fd, 0, SEEK_END);
    if (size < (off_t)HAL_STORAGE_SECTOR_SIZE) {
        printf("storage: %s has no medium\n", g_pick.node);
        close(fd);
        unlock();
        return HAL_STORAGE_NO_CARD;
    }
    g_fd = fd;
    g_fd_rw = false;
    g_sectors = (uint32_t)(size / HAL_STORAGE_SECTOR_SIZE);
    fill_info();
    if (g_write_enabled && !reopen(true)) {
        g_write_enabled = false;
    }
    printf("storage: %s, %u sectors (%s)%s\n", g_pick.node, (unsigned)g_sectors, g_info.pnm[0] ? g_info.pnm : "card",
           g_read_only_media ? ", write-protected" : "");
    unlock();
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    hal_storage_unmount();
    lock();
    if (g_fd_rw) {
        (void)fsync(g_fd);
    }
    close_device();
    unlock();
}

bool hal_storage_ready(void) {
    lock();
    bool r = g_fd >= 0 && g_sectors > 0;
    unlock();
    return r;
}

// Is a card in the slot? A pure sysfs look (silent, no open): the hotplug poll calls it every couple of seconds, and it
// must follow the physical card, not whether we still hold a descriptor.
bool hal_storage_card_present(void) {
    y2_sd_pick_t p;
    return y2_sd_pick(Y2_SYS_BLOCK, Y2_DEV_DIR, &p, NULL, 0) == Y2_SD_FOUND;
}

uint32_t hal_storage_sector_count(void) { return g_sectors; }

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    lock();
    bool ready = g_fd >= 0 && g_sectors > 0;
    if (ready) {
        *out = g_info;
    }
    unlock();
    return ready ? HAL_STORAGE_OK : HAL_STORAGE_NOT_INIT;
}

// Lock held. A failed transfer: if the card is gone (the kernel says so, or EIO while its sysfs entry has vanished), drop the
// descriptor. FatFs is NOT touched from here (we may be inside it); the next hal_storage_init() unmounts.
static hal_storage_status_t io_error(int err) {
    bool gone = err == ENOMEDIUM || err == ENODEV || err == ENXIO || (err == EIO && !hal_storage_card_present());
    if (gone) {
        printf("storage: card removed (%s)\n", strerror(err));
        close_device();
        return HAL_STORAGE_NO_CARD;
    }
    return HAL_STORAGE_IO;
}

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    lock();
    hal_storage_status_t st = HAL_STORAGE_OK;
    if (g_fd < 0 || g_sectors == 0) {
        st = HAL_STORAGE_NOT_INIT;
    } else if ((uint64_t)lba + count > g_sectors) {
        st = HAL_STORAGE_BAD_PARAM;
    } else {
        uint8_t *p = buf;
        size_t left = (size_t)count * HAL_STORAGE_SECTOR_SIZE;
        off_t off = (off_t)lba * HAL_STORAGE_SECTOR_SIZE;
        while (left > 0) {
            ssize_t r = pread(g_fd, p, left, off);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r <= 0) {
                st = io_error(r < 0 ? errno : EIO);
                break;
            }
            p += r;
            off += r;
            left -= (size_t)r;
        }
    }
    unlock();
    return st;
}

// Writes enabled: reopen read-write (validated). If that is refused the lock stays on and writes report write-protect.
void hal_storage_set_write_enabled(bool enabled) {
    lock();
    if (enabled) {
        g_write_enabled = g_fd < 0 || reopen(true); // before init: just remembered, applied by init
    } else {
        g_write_enabled = false;
        if (g_fd >= 0 && !reopen(false)) {
            // Could not go back to read-only (the card is gone or changed): drop it rather than keep a writable handle.
            close_device();
        }
    }
    unlock();
}

bool hal_storage_get_write_enabled(void) { return g_write_enabled; }

hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (!g_write_enabled) {
        return HAL_STORAGE_WRITE_PROTECT; // checked first: nothing below may run while locked
    }
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    lock();
    hal_storage_status_t st = HAL_STORAGE_OK;
    if (g_write_fault) {
        st = HAL_STORAGE_IO; // a failed write latches until hal_storage_init(), as on the Pico
    } else if (g_fd < 0 || g_sectors == 0) {
        st = HAL_STORAGE_NOT_INIT;
    } else if (g_read_only_media || !g_fd_rw) {
        st = HAL_STORAGE_WRITE_PROTECT;
    } else if ((uint64_t)lba + count > g_sectors) {
        st = HAL_STORAGE_BAD_PARAM;
    } else {
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
                st = io_error(w < 0 ? errno : EIO);
                break;
            }
            p += w;
            off += w;
            left -= (size_t)w;
        }
    }
    unlock();
    return st;
}

hal_storage_status_t hal_storage_sync(void) {
    lock();
    hal_storage_status_t st = HAL_STORAGE_OK;
    if (g_fd < 0) {
        st = HAL_STORAGE_NOT_INIT;
    } else if (g_fd_rw && fsync(g_fd) != 0) {
        st = HAL_STORAGE_IO;
    }
    unlock();
    return st;
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
