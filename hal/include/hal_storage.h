// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_storage.h
 * @brief Block storage (microSD over SDIO on hardware, a disk-image file on host).
 *
 * Shaped to sit directly under FatFs diskio. Sectors are 512 bytes.
 */
#ifndef NEUPOD_HAL_STORAGE_H
#define NEUPOD_HAL_STORAGE_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_STORAGE_SECTOR_SIZE 512u

/** Initialise the card / medium. Returns HAL_ENODEV if no card is present. */
hal_status_t hal_storage_init(void);

/** True once initialised and a medium is present. */
bool hal_storage_ready(void);

/** Total number of 512-byte sectors on the medium. */
uint32_t hal_storage_sector_count(void);

/** Read @p count sectors starting at @p lba into @p buf (count * 512 bytes). */
hal_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count);

/** Write @p count sectors starting at @p lba from @p buf. */
hal_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count);

/** Flush any write cache to the medium. */
hal_status_t hal_storage_sync(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_STORAGE_H
