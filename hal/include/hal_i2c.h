// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_i2c.h
 * @brief I2C master for on-board peripherals (fuel gauge, haptic driver).
 *
 * Addresses are 7-bit. All calls are blocking with a platform timeout.
 */
#ifndef NEUPOD_HAL_I2C_H
#define NEUPOD_HAL_I2C_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the peripheral bus at @p hz (e.g. 100000 or 400000). */
hal_status_t hal_i2c_init(uint32_t hz);

/** Write @p len bytes to device @p addr. */
hal_status_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len);

/** Read @p len bytes from device @p addr. */
hal_status_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len);

/** Write @p wlen bytes then repeated-start read @p rlen bytes (register read). */
hal_status_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata,
                                size_t rlen);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_I2C_H
