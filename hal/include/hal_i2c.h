// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_i2c.h
 * @brief I2C master for on-board peripherals (MAX17048 fuel gauge, DRV2605L haptic driver).
 *
 * Addresses are 7-bit (0x00..0x7F, not shifted). Every call is blocking but bounded: it never
 * waits forever. The timeout is derived from the byte count and the actual bus speed, with a
 * floor of HAL_I2C_TIMEOUT_FLOOR_US (see below). A timeout, or a failure that leaves SDA/SCL held
 * low, triggers bus recovery (see hal_i2c_recover()) before the call returns. The bus is single
 * threaded: do not call from two tasks/cores at once.
 *
 * Pull-ups are external (on the breakouts); the platform backends must not enable internal ones.
 * RP2350 wiring: I2C0, SDA GP20, SCL GP21 (see docs/wiring-phase1.md).
 */
#ifndef NEUPOD_HAL_I2C_H
#define NEUPOD_HAL_I2C_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Result of an I2C call. Zero is success; every error is negative. */
typedef enum {
    HAL_I2C_OK = 0,
    HAL_I2C_NACK_ADDR = -1, /**< No device acknowledged the address (absent, unpowered, wrong addr). */
    HAL_I2C_NACK_DATA = -2, /**< Device acknowledged the address but NACKed a data byte. */
    HAL_I2C_TIMEOUT = -3,   /**< Transaction did not finish in time (clock stretch, stuck bus). */
    HAL_I2C_BUS_STUCK = -4, /**< SDA or SCL held low and recovery failed. */
    HAL_I2C_ARB_LOST = -5,  /**< Arbitration lost. NOT detectable on RP2350 (the SDK clears the abort source), so never returned there; a lost arbitration that leaves a line low shows as BUS_STUCK. */
    HAL_I2C_BAD_PARAM = -6, /**< Bad address, zero/oversized length or NULL pointer. */
    HAL_I2C_NOT_INIT = -7,  /**< hal_i2c_init() has not been called. */
} hal_i2c_err_t;

/** Lowest timeout ever used for one transaction (clock stretching / slow device margin). */
#define HAL_I2C_TIMEOUT_FLOOR_US 2000u

/** First and last addresses probed by default; 0x00-0x07 and 0x78-0x7F are reserved by the spec. */
#define HAL_I2C_ADDR_FIRST 0x08u
#define HAL_I2C_ADDR_LAST 0x77u

/** Error and recovery counters since init / hal_i2c_reset_stats(). */
typedef struct {
    uint32_t transactions;      /**< Calls that reached the bus (all kinds, incl. probes). */
    uint32_t nack_addr;         /**< Address NACKs on write/read/write_read (NOT probes). */
    uint32_t nack_data;         /**< Data-byte NACKs. */
    uint32_t timeouts;          /**< Timed-out transactions (probes included). */
    uint32_t arb_lost;          /**< Arbitration losses. */
    uint32_t bus_stuck;         /**< Transactions that ended with a line held low after recovery. */
    uint32_t probe_nacks;       /**< Probes that got no ACK (normal while scanning; not an error). */
    uint32_t recoveries;        /**< Bit-bang bus recoveries attempted (SDA/SCL found low). */
    uint32_t recovery_failures; /**< Of those, how many did not free the bus. */
} hal_i2c_stats_t;

/**
 * Initialise the bus at @p bus_hz (e.g. 100000 or 400000) and configure the pins with internal
 * pulls OFF. If SDA is found held low, runs bus recovery first. Calling it again re-initialises.
 * Returns HAL_I2C_OK, HAL_I2C_BAD_PARAM (hz out of range) or HAL_I2C_BUS_STUCK (a line is still
 * low after recovery; the bus stays initialised so it can be inspected and retried).
 */
hal_i2c_err_t hal_i2c_init(uint32_t bus_hz);

/** Disable the peripheral and release both pins (inputs, no pulls). */
void hal_i2c_deinit(void);

/** Baud rate the hardware actually runs at (differs from the request by divider rounding); 0 if not initialised. */
uint32_t hal_i2c_get_baud(void);

/**
 * Change the bus speed (the 400 -> 100 kHz fallback helper). Never done automatically: the caller
 * decides. Returns the status; read the result with hal_i2c_get_baud().
 */
hal_i2c_err_t hal_i2c_set_baud(uint32_t bus_hz);

/** Sample the line levels (true = high). Both should be high on an idle bus. */
hal_i2c_err_t hal_i2c_bus_state(bool *sda_high, bool *scl_high);

/**
 * Bus recovery. Resets the controller; if SDA is low, switches the pins to GPIO and clocks up to 9
 * SCL pulses (drive low / release, never drive high) until SDA releases, then generates a STOP,
 * then restores the I2C function. HAL_I2C_OK if both lines are high afterwards, else
 * HAL_I2C_BUS_STUCK. Called internally after timeouts and at init; also callable by tests.
 */
hal_i2c_err_t hal_i2c_recover(void);

/**
 * Is anything at @p addr? Uses a 1-byte READ (a zero-length write is unreliable on RP2040/RP2350
 * hardware). Note the device sees a real read. Reserved addresses (0x00-0x07, 0x78-0x7F) are
 * refused with HAL_I2C_BAD_PARAM. Returns HAL_I2C_OK (ACK), HAL_I2C_NACK_ADDR (nothing there) or
 * another error. A NACK counts as stats.probe_nacks, not as an error.
 */
hal_i2c_err_t hal_i2c_probe(uint8_t addr);

/** Called by hal_i2c_scan() for every address that ACKs. */
typedef void (*hal_i2c_scan_cb)(uint8_t addr, void *ctx);

/**
 * Probe every address in [first, last] (clamped to 0x08..0x77), calling @p cb for each ACK.
 * @p found (optional) receives the number of ACKing addresses. Keeps going after a timeout (the
 * bus is recovered each time) and returns the first non-NACK error seen, or HAL_I2C_OK.
 */
hal_i2c_err_t hal_i2c_scan(uint8_t first, uint8_t last, hal_i2c_scan_cb cb, void *ctx, size_t *found);

/** Write @p len (1..HAL_I2C_MAX_XFER) bytes to @p addr (reserved addresses refused with BAD_PARAM), ending with STOP. */
hal_i2c_err_t hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len);

/** Read @p len (1..HAL_I2C_MAX_XFER) bytes from @p addr, ending with STOP. */
hal_i2c_err_t hal_i2c_read(uint8_t addr, uint8_t *data, size_t len);

/** Write @p wlen bytes, repeated START (no STOP in between), read @p rlen bytes. */
hal_i2c_err_t hal_i2c_write_read(uint8_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata,
                                 size_t rlen);

/** Longest single transfer accepted (bytes). */
#define HAL_I2C_MAX_XFER 256u

/** @name Register helpers (device register pointer is one byte)
 * @{ */
hal_i2c_err_t hal_i2c_reg_read8(uint8_t addr, uint8_t reg, uint8_t *value);
hal_i2c_err_t hal_i2c_reg_write8(uint8_t addr, uint8_t reg, uint8_t value);
/** 16-bit big-endian (MSB at @p reg, LSB at reg+1), as the MAX17048 uses. */
hal_i2c_err_t hal_i2c_reg_read16_be(uint8_t addr, uint8_t reg, uint16_t *value);
hal_i2c_err_t hal_i2c_reg_write16_be(uint8_t addr, uint8_t reg, uint16_t value);
/** Read @p len bytes starting at @p reg in one repeated-start transaction. */
hal_i2c_err_t hal_i2c_reg_read_burst(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
/** Write @p len (1..32) bytes to consecutive registers starting at @p reg. */
hal_i2c_err_t hal_i2c_reg_write_burst(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t len);
/** @} */

/** Copy the counters out. */
void hal_i2c_get_stats(hal_i2c_stats_t *out);
void hal_i2c_reset_stats(void);

/** Short name for logs, e.g. "NACK_ADDR". */
const char *hal_i2c_strerror(hal_i2c_err_t err);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_I2C_H
