// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_storage.h
 * @brief Block storage (microSD over 4-bit SDIO on hardware, a disk-image file on host).
 *
 * Shaped to sit directly under FatFs diskio. Sectors are 512 bytes. Nothing here asserts:
 * every failure comes back as a hal_storage_status_t.
 *
 * WRITE SAFETY: the card may hold user data, so writes are locked out by default. Both
 * hal_storage_write() and (on RP2350) the FatFs write path fail with
 * HAL_STORAGE_WRITE_PROTECT until hal_storage_set_write_enabled(true) is called. Read-only
 * code (card detection, info, sector reads, mount) never needs it.
 */
#ifndef NEUPOD_HAL_STORAGE_H
#define NEUPOD_HAL_STORAGE_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_STORAGE_SECTOR_SIZE 512u

/** Result of every storage call. Zero is success. */
typedef enum {
    HAL_STORAGE_OK = 0,
    HAL_STORAGE_NO_CARD,       /**< No card in the slot / no response to the first commands. */
    HAL_STORAGE_TIMEOUT,       /**< Card or bus did not respond in time. */
    HAL_STORAGE_CRC,           /**< Command/data CRC error on the bus. */
    HAL_STORAGE_IO,            /**< Other bus or card error. */
    HAL_STORAGE_NOT_INIT,      /**< hal_storage_init() has not succeeded. */
    HAL_STORAGE_NOT_MOUNTED,   /**< No FatFs volume is mounted. */
    HAL_STORAGE_NO_FILESYSTEM, /**< Card is fine but holds no FAT/exFAT volume. */
    HAL_STORAGE_UNSUPPORTED,   /**< Not available on this platform or card. */
    HAL_STORAGE_BAD_PARAM,     /**< Invalid argument (NULL, out of range, ...). */
    HAL_STORAGE_WRITE_PROTECT, /**< Write attempted while writes are locked (or card is RO). */
    HAL_STORAGE_BUSY,          /**< Resources for the driver are not available. */
} hal_storage_status_t;

/** Short upper-case name of a status, e.g. "NO_CARD". Never NULL. */
const char *hal_storage_status_str(hal_storage_status_t st);

typedef enum {
    HAL_CARD_NONE = 0,
    HAL_CARD_SDSC, /**< Standard capacity, up to 2 GB (byte addressed). */
    HAL_CARD_SDHC, /**< High capacity, 2 GB to 32 GB. */
    HAL_CARD_SDXC, /**< Extended capacity, above 32 GB. */
    HAL_CARD_IMAGE /**< Host only: a disk-image file standing in for a card. */
} hal_card_type_t;

/** Name of a card type, e.g. "SDXC". Never NULL. */
const char *hal_storage_card_type_str(hal_card_type_t t);

/** Everything read from the card at identification time (all read-only registers). */
typedef struct {
    hal_card_type_t type;
    uint32_t sectors;        /**< Capacity in 512-byte sectors. */
    uint64_t capacity_bytes; /**< sectors * 512. */

    /* CID */
    uint8_t mid;       /**< Manufacturer ID. */
    char oid[3];       /**< OEM/application ID, 2 chars + NUL. */
    char pnm[6];       /**< Product name, 5 chars + NUL. */
    uint8_t prv_major; /**< Product revision, BCD high nibble. */
    uint8_t prv_minor; /**< Product revision, BCD low nibble. */
    uint32_t psn;      /**< Serial number. */
    uint16_t mdt_year; /**< Manufacture year (2000 + code). */
    uint8_t mdt_month; /**< Manufacture month 1..12. */

    /* CSD */
    uint8_t csd_version;     /**< CSD_STRUCTURE: 0 = v1.0 (SDSC), 1 = v2.0 (SDHC/SDXC). */
    uint8_t tran_speed_raw;  /**< TRAN_SPEED byte as read (0x32 = 25 MHz, 0x5A = 50 MHz, ...). */
    uint32_t max_bus_hz;     /**< Max clock the card claims in TRAN_SPEED (0 if unknown). */
    bool write_protected;    /**< Permanent or temporary write-protect bit set in CSD. */

    /* SD Status register (SSR); valid only if ssr_valid. */
    bool ssr_valid;
    uint8_t speed_class;     /**< 0 (none), 2, 4, 6 or 10. */
    uint8_t uhs_speed_grade; /**< 0 (none), 1 (U1) or 3 (U3). */
    uint8_t video_speed_class; /**< 0 (none) or 6, 10, 30, 60, 90. */
    uint32_t au_size_bytes;  /**< Allocation unit; 0 = not reported. Use for mkfs alignment. */

    /* Interface */
    uint8_t bus_width;       /**< Data lines in use (4 on the testbed; 1 for host image). */
    uint32_t bus_clock_hz;   /**< Actual SDIO clock now (0 for the host image). */
    bool high_speed;         /**< Card switched to High Speed mode (CMD6); allows clocks above 25 MHz. */
} hal_card_info_t;

/**
 * Initialise the interface and identify the card (SD init sequence, reads CID/CSD, sets the
 * 4-bit bus and the configured clock). READ-ONLY: writes nothing to the card. Returns within a
 * bounded time (about a second) if no card answers. Safe to call again to re-identify.
 */
hal_storage_status_t hal_storage_init(void);

/** Release the card (unmounts first). The interface can be brought up again with init. */
void hal_storage_deinit(void);

/** True once hal_storage_init() has succeeded and the card is still initialised. */
bool hal_storage_ready(void);

/**
 * Probe for a card. Without a card-detect pin this talks to the card (a status request when
 * initialised; otherwise a light identification attempt). Does not write.
 */
bool hal_storage_card_present(void);

/** Fill @p out from the identified card. HAL_STORAGE_NOT_INIT if not initialised. */
hal_storage_status_t hal_storage_get_info(hal_card_info_t *out);

/** Total number of 512-byte sectors (0 if not initialised). */
uint32_t hal_storage_sector_count(void);

/** Read @p count sectors starting at @p lba into @p buf (count * 512 bytes). Read-only. */
hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count);

/** Lock/unlock all writes (raw and FatFs). Default: locked. */
void hal_storage_set_write_enabled(bool enabled);
bool hal_storage_get_write_enabled(void);

/**
 * Write @p count sectors starting at @p lba. HAL_STORAGE_WRITE_PROTECT unless writes were
 * enabled with hal_storage_set_write_enabled(true). Never call from read-only tests.
 */
hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count);

/** Flush any write cache to the medium. */
hal_storage_status_t hal_storage_sync(void);

/**
 * Set the SDIO bus clock in Hz. Takes effect immediately if a card is initialised, otherwise
 * at the next init. The PIO clock divider is fractional, so the result is the nearest
 * achievable rate; read it back with hal_storage_get_bus_clock(). On RP2350 the maximum is
 * clk_sys / 4 (31.25 MHz at 125 MHz). HAL_STORAGE_BAD_PARAM if out of range. Above 25 MHz
 * (the SD default-speed limit) the call fails with HAL_STORAGE_UNSUPPORTED unless the card was
 * switched with hal_storage_enable_high_speed(). On a failure to re-establish the bus the card
 * must be re-initialised (hal_storage_init() also resets the clock to the default). Host:
 * HAL_STORAGE_UNSUPPORTED.
 */
hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz);

/**
 * Ask the card to switch to High Speed mode (CMD6, function group 1; raises the card's maximum
 * clock from 25 to 50 MHz). Reads the card's support bits first. Does not write card data.
 * HAL_STORAGE_UNSUPPORTED if the card does not offer it. Lasts until the next hal_storage_init().
 */
hal_storage_status_t hal_storage_enable_high_speed(void);

/** Actual bus clock in Hz now (or that will be used at next init); 0 on the host. */
uint32_t hal_storage_get_bus_clock(void);

/** Volume type found by hal_storage_mount(). */
typedef enum {
    HAL_FS_NONE = 0,
    HAL_FS_FAT12,
    HAL_FS_FAT16,
    HAL_FS_FAT32,
    HAL_FS_EXFAT
} hal_fs_type_t;

/**
 * Mount the FatFs volume (reads only: boot sector / FAT). HAL_STORAGE_NO_FILESYSTEM if the card
 * has none. RP2350 only for now; the host returns HAL_STORAGE_UNSUPPORTED.
 */
hal_storage_status_t hal_storage_mount(void);
void hal_storage_unmount(void);
bool hal_storage_is_mounted(void);
/** Type of the mounted volume (HAL_FS_NONE if not mounted). */
hal_fs_type_t hal_storage_fs_type(void);

/** Facts about the mounted volume (not the card). */
typedef struct {
    hal_fs_type_t fs;
    uint32_t cluster_bytes;
    uint32_t sectors_per_cluster;
    char label[36];  /**< Volume label (UTF-8), empty if none. */
    uint32_t serial; /**< Volume serial number. */
} hal_volume_info_t;

/** HAL_STORAGE_NOT_MOUNTED if no volume is mounted; UNSUPPORTED on the host. Read-only. */
hal_storage_status_t hal_storage_volume_info(hal_volume_info_t *out);

typedef struct {
    uint32_t cluster_bytes;  /**< exFAT cluster size, power of two; 0 = 131072 (128 KB). */
    uint32_t align_sectors;  /**< Partition start and data-area alignment in 512-byte sectors,
                                  power of two; 0 = the card's allocation unit from the SD Status
                                  register, or 8192 (4 MiB) if the card does not report one. */
    bool erase_all_confirmed; /**< Must be true: the caller asserts the user typed the confirmation. */
} hal_format_opts_t;

/**
 * DESTRUCTIVE. Erase the card's partition table and create the project layout: an MBR with ONE
 * exFAT partition (type 0x07) starting at the alignment boundary and covering the rest of the
 * card in whole allocation units, exFAT with the chosen cluster size and its data area aligned the
 * same way (the layout the SD Association formatter produces), then set the volume label
 * (@p label, NULL = "NEUPOD"). Everything on the card is lost, including other partitions such as
 * a Linux root file system.
 *
 * Unmounts first, unlocks writes only for its own duration (they are locked again, and the volume
 * left unmounted, on every return path). HAL_STORAGE_BAD_PARAM unless
 * @p opts->erase_all_confirmed. Takes tens of seconds on a large card. UNSUPPORTED on the host.
 * The card must be initialised; afterwards call hal_storage_mount().
 */
hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts);

/**
 * Query the card status (CMD13, read-only): HAL_STORAGE_OK if no error bits are set and the card
 * is in the transfer state; *r1 (may be NULL) gets the raw R1 status word.
 */
hal_storage_status_t hal_storage_card_status(uint32_t *r1);

/**
 * True after a write failed, or the status after a write looked wrong. The library ignores write
 * status and can leave the card waiting for data, so ALL further writes (raw and FatFs) are
 * refused until hal_storage_init() succeeds (its reset clears the latch). Callers must re-init and
 * remount before cleaning up.
 */
bool hal_storage_write_fault(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_STORAGE_H
