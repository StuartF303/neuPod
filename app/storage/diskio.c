// SPDX-License-Identifier: MPL-2.0
/**
 * FatFs low-level glue on top of hal_storage. Single physical drive (pdrv 0): microSD on
 * hardware, a disk-image file on the host.
 */
#include "ff.h"
#include "diskio.h"
#include "hal_storage.h"

#define NEUPOD_PDRV 0

#if FF_MULTI_PARTITION
/* Logical drive 0 -> physical drive 0, partition 0 = auto-detect. (The RP2350 build defines its own
   table in platforms/rp2350/hal/hal_storage.c; this file is not linked there.) */
PARTITION VolToPart[FF_VOLUMES] = {{NEUPOD_PDRV, 0}};
#endif

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != NEUPOD_PDRV || !hal_storage_ready()) {
        return STA_NOINIT;
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != NEUPOD_PDRV) {
        return STA_NOINIT;
    }
    if (!hal_storage_ready() && hal_storage_init() != HAL_STORAGE_OK) {
        return STA_NOINIT;
    }
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != NEUPOD_PDRV || !hal_storage_ready()) {
        return RES_NOTRDY;
    }
    return hal_storage_read((uint32_t)sector, buff, count) == HAL_STORAGE_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != NEUPOD_PDRV || !hal_storage_ready()) {
        return RES_NOTRDY;
    }
    return hal_storage_write((uint32_t)sector, buff, count) == HAL_STORAGE_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != NEUPOD_PDRV || !hal_storage_ready()) {
        return RES_NOTRDY;
    }
    switch (cmd) {
    case CTRL_SYNC:
        return hal_storage_sync() == HAL_STORAGE_OK ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = hal_storage_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = HAL_STORAGE_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1; // erase block size unknown/irrelevant for an image file
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
