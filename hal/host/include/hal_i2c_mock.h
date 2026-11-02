// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_i2c_mock.h
 * @brief Host-only control surface of the simulated I2C bus (hal/host/hal_i2c.c).
 *
 * Register fake devices, inject faults, then exercise drivers through the normal hal_i2c.h API.
 * Not part of the portable HAL; never include from app/ code that also builds on the device.
 */
#ifndef NEUPOD_HAL_I2C_MOCK_H
#define NEUPOD_HAL_I2C_MOCK_H

#include "hal_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Byte-stream device. Return HAL_I2C_OK to ACK, HAL_I2C_NACK_DATA to NACK data. Either may be NULL (NACK). */
typedef struct {
    hal_i2c_err_t (*write)(void *ctx, const uint8_t *data, size_t len);
    hal_i2c_err_t (*read)(void *ctx, uint8_t *data, size_t len);
    void *ctx;
} hal_i2c_mock_ops_t;

/** Called after each register byte written to a register-map device. */
typedef void (*hal_i2c_mock_reg_write_cb)(void *ctx, uint8_t reg, uint8_t value);

/** Add a device with custom behaviour. Returns false if the address is taken or the table is full. */
bool hal_i2c_mock_add_device(uint8_t addr, const hal_i2c_mock_ops_t *ops);

/**
 * Add a register-array device: first written byte sets the register pointer, further bytes are
 * stored at pointer++; reads return regs[pointer++] (0xFF beyond @p nregs). @p regs is owned by
 * the caller and stays valid. @p on_write (optional) is called per stored byte, after the store.
 */
bool hal_i2c_mock_add_regmap(uint8_t addr, uint8_t *regs, size_t nregs,
                             hal_i2c_mock_reg_write_cb on_write, void *ctx);

/** Remove all devices, faults and counters. */
void hal_i2c_mock_reset(void);

/** Make the next @p count transactions (probes included) fail with @p err (a negative error). */
void hal_i2c_mock_inject_error(hal_i2c_err_t err, uint32_t count);

/** Hold SDA low until bus recovery has clocked @p pulses_needed SCL pulses (> 9 never frees). 0 = free. */
void hal_i2c_mock_set_stuck_sda(uint32_t pulses_needed);

/** Number of transactions that reached the device model (including NACKed). */
uint32_t hal_i2c_mock_transaction_count(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_I2C_MOCK_H
