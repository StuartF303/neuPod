// SPDX-License-Identifier: MPL-2.0
/*
 * microSD over SPI via carlk3's no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library (the Waveshare RP2350-Touch-AMOLED-2.41 wires its TF slot
 * for SPI0, see hw_config.c). The SPI counterpart of platforms/rp2350/hal/hal_storage.c (SDIO): the same hal_storage.h contract,
 * the same write guard (nothing reaches the card unless hal_storage_set_write_enabled(true) was called, and a failed write locks
 * writes until the next successful hal_storage_init()), the same mount / format code. What differs: SPI mode has no card status
 * register read (hal_storage_card_status), no SD Status register (no speed class or allocation unit in hal_card_info_t), no bus
 * clock changes, no High Speed switch; and it only accepts SDHC / SDXC cards (the library refuses standard-capacity ones).
 */
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "hal_storage.h"
#include "hal_storage_decode.h"
#include "hardware/spi.h"
#include "hw_config.h"
#include "pico/stdlib.h"
#include "sd_card.h"

static sd_card_t *g_sd;
static bool g_write_enabled;
static bool g_mounted;
static bool g_write_fault; // latched by a failed write; cleared only by a successful init
static hal_card_info_t g_info;
static block_dev_err_t (*g_lib_write)(sd_card_t *, const uint8_t *, uint32_t, uint32_t);
static uint32_t g_bounce[HAL_STORAGE_SECTOR_SIZE / 4]; // aligned scratch sector for mbr / format

#define SPI_READ_CHUNK 128u // sectors per library call

static bool initialised(void) { return g_sd != NULL && !(g_sd->state.m_Status & STA_NOINIT); }

// Library write hook: refuse unless writes are enabled; after ANY failure latch g_write_fault and refuse every further write until
// hal_storage_init() succeeds (its identification resets the card), so a half-finished multi-block write is never continued.
static block_dev_err_t guarded_write(sd_card_t *sd, const uint8_t *buf, uint32_t sector, uint32_t count) {
    if (g_write_fault) {
        return SD_BLOCK_DEVICE_ERROR_WRITE;
    }
    if (!g_write_enabled) {
        return SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED;
    }
    if (g_lib_write == NULL || g_lib_write == guarded_write) {
        return SD_BLOCK_DEVICE_ERROR_WRITE;
    }
    block_dev_err_t r = g_lib_write(sd, buf, sector, count);
    if (r != SD_BLOCK_DEVICE_ERROR_NONE) {
        g_write_fault = true;
    }
    return r;
}

// Idempotent: saving a hook again after it was replaced would make the guard call itself forever.
static void install_write_guard(sd_card_t *sd) {
    if (sd->write_blocks != guarded_write) {
        g_lib_write = sd->write_blocks;
        sd->write_blocks = guarded_write;
    }
}

static hal_storage_status_t map_block_err(block_dev_err_t e) {
    switch (e) {
    case SD_BLOCK_DEVICE_ERROR_NONE: return HAL_STORAGE_OK;
    case SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED: return HAL_STORAGE_WRITE_PROTECT;
    case SD_BLOCK_DEVICE_ERROR_NO_DEVICE:
    case SD_BLOCK_DEVICE_ERROR_NO_INIT: return HAL_STORAGE_NO_CARD;
    case SD_BLOCK_DEVICE_ERROR_NO_RESPONSE: return HAL_STORAGE_TIMEOUT;
    case SD_BLOCK_DEVICE_ERROR_CRC: return HAL_STORAGE_CRC;
    default: return HAL_STORAGE_IO;
    }
}

// One-time driver bring-up (sets up the SPI and the chip select, sends the "go idle" clocks). Does not read the card.
static hal_storage_status_t ensure_driver(void) {
    if (g_sd != NULL) {
        return HAL_STORAGE_OK;
    }
    sd_card_t *sd = sd_get_by_num(0);
    bool ok = sd != NULL && sd_init_driver();
    if (sd != NULL) {
        install_write_guard(sd);
    }
    if (!ok) {
        return HAL_STORAGE_IO;
    }
    g_sd = sd;
    return HAL_STORAGE_OK;
}

hal_storage_status_t hal_storage_init(void) {
    hal_storage_status_t st = ensure_driver();
    if (st != HAL_STORAGE_OK) {
        return st;
    }
    hal_storage_unmount();
    install_write_guard(g_sd);
    if (initialised()) {
        g_sd->sync(g_sd); // close an open multi-block write before the card is reset
    }
    // Three attempts (a card that was just inserted, or one left mid-command by a reset). A missing card fails in well under a second.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            printf("storage: card did not identify, attempt %d\n", attempt + 1);
            sleep_ms(50);
        }
        if (initialised()) {
            g_sd->deinit(g_sd); // forces a fresh identification below
        }
        g_sd->spi_if_p->state.ongoing_mlt_blk_wrt = false; // CMD0 resets the card: forget any open write
        g_sd->init(g_sd);
        if (initialised()) {
            break;
        }
    }
    if (!initialised()) {
        // Something answers but cannot be used (a standard-capacity card, a bad card) vs nothing answers at all.
        return g_sd->sd_test_com(g_sd) ? HAL_STORAGE_IO : HAL_STORAGE_NO_CARD;
    }

    memset(&g_info, 0, sizeof g_info);
    hal_storage_decode_cid(g_sd->state.CID, &g_info);
    if (!hal_storage_decode_csd(g_sd->state.CSD, &g_info)) {
        return HAL_STORAGE_UNSUPPORTED;
    }
    if (g_sd->state.sectors != 0) {
        g_info.sectors = g_sd->state.sectors; // the library's own count (agrees with CSD)
        g_info.capacity_bytes = (uint64_t)g_info.sectors * HAL_STORAGE_SECTOR_SIZE;
    }
    g_info.ssr_valid = false; // no SD Status register in this driver
    g_info.bus_width = 1;
    g_info.bus_clock_hz = spi_get_baudrate(g_sd->spi_if_p->spi->hw_inst);
    g_info.high_speed = false;
    g_write_fault = false; // identification has reset the card
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    if (g_sd == NULL) {
        return;
    }
    hal_storage_unmount();
    if (initialised()) {
        g_sd->sync(g_sd);
        g_sd->deinit(g_sd);
    }
}

bool hal_storage_ready(void) { return initialised(); }

bool hal_storage_card_present(void) {
    if (ensure_driver() != HAL_STORAGE_OK) {
        return false;
    }
    return g_sd->sd_test_com(g_sd);
}

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    *out = g_info;
    return HAL_STORAGE_OK;
}

uint32_t hal_storage_sector_count(void) { return initialised() ? g_info.sectors : 0; }

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if ((uint64_t)lba + count > g_info.sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    uint8_t *dst = buf;
    while (count > 0) {
        uint32_t n = count < SPI_READ_CHUNK ? count : SPI_READ_CHUNK;
        hal_storage_status_t st = map_block_err(g_sd->read_blocks(g_sd, dst, lba, n));
        if (st != HAL_STORAGE_OK) {
            return st;
        }
        dst += (size_t)n * HAL_STORAGE_SECTOR_SIZE;
        lba += n;
        count -= n;
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
        return HAL_STORAGE_IO; // see guarded_write(): needs hal_storage_init()
    }
    if (buf == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_info.write_protected) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    if ((uint64_t)lba + count > g_info.sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    const uint8_t *src = buf;
    while (count > 0) {
        uint32_t n = count < SPI_READ_CHUNK ? count : SPI_READ_CHUNK;
        hal_storage_status_t st = map_block_err(g_sd->write_blocks(g_sd, src, lba, n));
        if (st != HAL_STORAGE_OK) {
            return st;
        }
        src += (size_t)n * HAL_STORAGE_SECTOR_SIZE;
        lba += n;
        count -= n;
    }
    return HAL_STORAGE_OK;
}

hal_storage_status_t hal_storage_sync(void) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    return map_block_err(g_sd->sync(g_sd));
}

// SPI mode: the clock is fixed (hw_config.c) and there is no CMD6 here.
hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz) {
    (void)hz;
    return HAL_STORAGE_UNSUPPORTED;
}

hal_storage_status_t hal_storage_enable_high_speed(void) { return HAL_STORAGE_UNSUPPORTED; }

hal_storage_status_t hal_storage_card_status(uint32_t *r1_out) {
    (void)r1_out;
    return HAL_STORAGE_UNSUPPORTED;
}

bool hal_storage_write_fault(void) { return g_write_fault; }

uint32_t hal_storage_get_bus_clock(void) {
    sd_card_t *sd = g_sd != NULL ? g_sd : sd_get_by_num(0);
    return sd != NULL ? spi_get_baudrate(sd->spi_if_p->spi->hw_inst) : 0;
}

hal_storage_status_t hal_storage_mount(void) {
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    FRESULT fr = f_mount(&g_sd->state.fatfs, sd_get_drive_prefix(g_sd), 1); // reads only
    switch (fr) {
    case FR_OK:
        g_mounted = true;
        return HAL_STORAGE_OK;
    case FR_NO_FILESYSTEM: return HAL_STORAGE_NO_FILESYSTEM;
    case FR_NOT_READY: return HAL_STORAGE_NO_CARD;
    case FR_DISK_ERR: return HAL_STORAGE_IO;
    default: return HAL_STORAGE_IO;
    }
}

void hal_storage_unmount(void) {
    if (g_sd != NULL && g_mounted) {
        f_unmount(sd_get_drive_prefix(g_sd));
        g_mounted = false;
    }
}

bool hal_storage_is_mounted(void) { return g_mounted && initialised(); }

hal_fs_type_t hal_storage_fs_type(void) {
    if (!hal_storage_is_mounted()) {
        return HAL_FS_NONE;
    }
    switch (g_sd->state.fatfs.fs_type) {
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
    out->sectors_per_cluster = g_sd->state.fatfs.csize;
    out->cluster_bytes = out->sectors_per_cluster * HAL_STORAGE_SECTOR_SIZE;
    TCHAR label[36] = {0}; // FatFs needs at least 34 bytes
    DWORD vsn = 0;
    if (f_getlabel(sd_get_drive_prefix(g_sd), label, &vsn) != FR_OK) {
        return HAL_STORAGE_IO;
    }
    snprintf(out->label, sizeof out->label, "%s", label);
    out->serial = vsn;
    return HAL_STORAGE_OK;
}

// Work buffer for f_mkfs (it is also handed to the disk layer, so it must be word aligned).
static uint8_t g_mkfs_work[32 * 1024] __attribute__((aligned(4)));

// Logical drive 0 -> physical drive 0, partition 0 = auto-detect. hal_storage_format() sets 1 to make f_mkfs use partition 1 of the
// MBR it wrote. FatFs requires this table when FF_MULTI_PARTITION is on.
PARTITION VolToPart[FF_VOLUMES] = {{0, 0}};

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// MBR with one exFAT/NTFS-type (0x07) partition at start_lba (f_fdisk cannot place the partition, so the table is written by hand).
static void build_mbr(uint8_t *s, uint32_t start_lba, uint32_t sectors, uint32_t disk_id) {
    memset(s, 0, HAL_STORAGE_SECTOR_SIZE);
    put_le32(&s[440], disk_id);
    uint8_t *e = &s[446];
    e[0] = 0x00;                           // not bootable
    e[1] = 0xFE, e[2] = 0xFF, e[3] = 0xFF; // CHS start: "use LBA" marker
    e[4] = 0x07;                           // exFAT / NTFS / HPFS
    e[5] = 0xFE, e[6] = 0xFF, e[7] = 0xFF; // CHS end
    put_le32(&e[8], start_lba);
    put_le32(&e[12], sectors);
    s[510] = 0x55;
    s[511] = 0xAA;
}

hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts) {
    if (opts == NULL || !opts->erase_all_confirmed) {
        return HAL_STORAGE_BAD_PARAM; // the caller must state that it has the user's confirmation
    }
    if (!initialised()) {
        return HAL_STORAGE_NOT_INIT;
    }
    if (g_info.write_protected) {
        return HAL_STORAGE_WRITE_PROTECT;
    }
    uint32_t cluster = opts->cluster_bytes ? opts->cluster_bytes : 131072u;
    uint32_t align = opts->align_sectors ? opts->align_sectors
                                         : (g_info.au_size_bytes ? g_info.au_size_bytes / HAL_STORAGE_SECTOR_SIZE : 8192u); // 4 MiB fallback
    if (cluster < HAL_STORAGE_SECTOR_SIZE || (cluster & (cluster - 1u)) != 0 || (align & (align - 1u)) != 0 ||
        g_info.sectors <= 2u * align) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (label == NULL || label[0] == '\0') {
        label = "NEUPOD";
    }
    hal_storage_unmount();
    bool prev_enabled = g_write_enabled;
    g_write_enabled = true; // unlocked only for the duration of this function
    hal_storage_status_t st = HAL_STORAGE_OK;

    // 1. MBR: one partition from sector `align`, length a whole number of allocation units.
    uint32_t part_sectors = (g_info.sectors - align) / align * align;
    build_mbr((uint8_t *)g_bounce, align, part_sectors, g_info.psn);
    st = hal_storage_write(0, g_bounce, 1);

    // 2. exFAT inside partition 1, data area aligned, 128 KB clusters.
    if (st == HAL_STORAGE_OK) {
        VolToPart[0].pt = 1;
        MKFS_PARM opt = {.fmt = FM_EXFAT, .n_fat = 1, .align = align, .n_root = 0, .au_size = cluster};
        FRESULT fr = f_mkfs(sd_get_drive_prefix(g_sd), &opt, g_mkfs_work, sizeof g_mkfs_work);
        VolToPart[0].pt = 0;
        if (fr != FR_OK) {
            st = fr == FR_NOT_READY ? HAL_STORAGE_NO_CARD : HAL_STORAGE_IO;
        }
    }
    // 3. Label (needs a mounted volume), then leave unmounted.
    if (st == HAL_STORAGE_OK) {
        st = hal_storage_mount();
    }
    if (st == HAL_STORAGE_OK) {
        char path[48];
        snprintf(path, sizeof path, "%s%s", sd_get_drive_prefix(g_sd), label);
        st = f_setlabel(path) == FR_OK ? HAL_STORAGE_OK : HAL_STORAGE_IO;
    }
    hal_storage_unmount();
    g_write_enabled = prev_enabled;
    return st;
}
