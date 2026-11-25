// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_bt_addr.h
 * @brief The Y2's Bluetooth device address: the first 6 bytes of /data/BT_Addr (MSB first, as BTstack keeps it).
 *
 * Read only. Nothing in the firmware ever writes /data/BT_Addr or /data/nvram. An unreadable, short, all-zero or all-0xFF file
 * (erased NVRAM) is refused: the chip's own default address would be the same on every unit, so Bluetooth stays unavailable
 * instead (fail closed).
 */
#ifndef NEUPOD_Y2_BT_ADDR_H
#define NEUPOD_Y2_BT_ADDR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Y2_BT_ADDR_PATH "/data/BT_Addr"

typedef enum {
    Y2_BT_ADDR_OK = 0,
    Y2_BT_ADDR_UNREADABLE, /**< cannot be opened or read */
    Y2_BT_ADDR_SHORT,      /**< fewer than 6 bytes */
    Y2_BT_ADDR_INVALID,    /**< all zero or all 0xFF */
} y2_bt_addr_result_t;

/** Read the address from @p path (Y2_BT_ADDR_PATH on the device) into @p out. @p out is untouched unless OK. */
y2_bt_addr_result_t y2_bt_addr_read(const char *path, uint8_t out[6]);

/** A short phrase for a result, for the console. */
const char *y2_bt_addr_result_text(y2_bt_addr_result_t r);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_BT_ADDR_H
