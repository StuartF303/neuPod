// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_storage_decode.h
 * @brief Pure decoders for the SD card's CID, CSD and SD Status registers (shared HAL code,
 *        unit-tested on the host). Register bytes are in wire order: byte 0 holds the top bits
 *        (CID/CSD bits 127:120, SSR bits 511:504).
 */
#ifndef NEUPOD_HAL_STORAGE_DECODE_H
#define NEUPOD_HAL_STORAGE_DECODE_H

#include "hal_storage.h"

/** Fill the CID fields of @p info from the 16-byte register. */
void hal_storage_decode_cid(const uint8_t cid[16], hal_card_info_t *info);

/**
 * Fill csd_version, tran_speed_raw, max_bus_hz, write_protected, sectors, capacity_bytes and
 * type from the 16-byte CSD. Returns false for an unsupported CSD structure (sectors left 0).
 */
bool hal_storage_decode_csd(const uint8_t csd[16], hal_card_info_t *info);

/** Fill the SSR fields (speed class, UHS grade, video class, AU size) from the 64-byte status. */
void hal_storage_decode_ssr(const uint8_t ssr[64], hal_card_info_t *info);

#endif // NEUPOD_HAL_STORAGE_DECODE_H
