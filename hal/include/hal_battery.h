// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_battery.h
 * @brief LiPo fuel gauge (MAX17048 at I2C 0x36), portable over hal_i2c.
 *
 * Implemented in app/drivers/max17048.c. The I2C bus must already be initialised
 * (hal_i2c_init). The gauge is powered from the battery side: with no/flat battery it does not
 * ACK and every call returns HAL_ENODEV. Fixed-point only, so host tests are exact.
 *
 * Status codes: HAL_OK, HAL_ENODEV (no ACK / wrong chip), HAL_ETIMEOUT, HAL_EINVAL, HAL_ERR.
 */
#ifndef NEUPOD_HAL_BATTERY_H
#define NEUPOD_HAL_BATTERY_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Alert flag bits in hal_battery_status_t.alerts (from the gauge STATUS register). */
#define HAL_BATTERY_ALERT_RESET 0x01u       /**< RI: gauge was reset/powered up since last clear. */
#define HAL_BATTERY_ALERT_VOLT_HIGH 0x02u   /**< VH: cell voltage above VALRT max. */
#define HAL_BATTERY_ALERT_VOLT_LOW 0x04u    /**< VL: cell voltage below VALRT min. */
#define HAL_BATTERY_ALERT_VOLT_RESET 0x08u  /**< VR: voltage dropped below reset threshold. */
#define HAL_BATTERY_ALERT_SOC_LOW 0x10u     /**< HD: state of charge fell to the ATHD threshold. */
#define HAL_BATTERY_ALERT_SOC_CHANGE 0x20u  /**< SC: SOC changed by 1 % (needs ALSC enabled). */

/** Cell voltage at or above which a battery is considered present. */
#define HAL_BATTERY_PRESENT_MV 2500u

typedef struct {
    bool valid;            /**< All registers were read this call. */
    bool present;          /**< valid && mv >= HAL_BATTERY_PRESENT_MV. */
    uint16_t mv;           /**< Cell voltage, mV (78.125 uV/LSB, rounded). */
    uint16_t soc_x256;     /**< State of charge in 1/256 %: 100 % = 25600. Raw register value. */
    uint8_t soc_pct;       /**< soc_x256 / 256, clamped to 100. */
    int32_t rate_mpct_per_h; /**< Charge/discharge rate, milli-percent per hour (0.208 %/h per LSB, signed; negative = discharging). */
    uint8_t alerts;        /**< HAL_BATTERY_ALERT_* bits. */
} hal_battery_status_t;

/**
 * Probe the gauge and check VERSION reads 0x001X (HAL_ENODEV if absent or another chip). Side effect:
 * clears STATUS.RI (the power-up/reset indicator) if set, so a later RI means a new reset.
 */
hal_status_t hal_battery_init(void);

/** Raw VERSION register (0x001X on a MAX17048). */
hal_status_t hal_battery_get_version(uint16_t *version);

/** Raw CONFIG register (RCOMP in the high byte; SLEEP/ALSC/ALRT/ATHD in the low byte). */
hal_status_t hal_battery_get_config(uint16_t *config);

/** Raw HIBRT register (hibernate threshold high byte, active threshold low byte). */
hal_status_t hal_battery_get_hibrt(uint16_t *hibrt);

/** Read voltage, SOC, rate and alert flags. On error out->valid is false. */
hal_status_t hal_battery_read(hal_battery_status_t *out);

/** Low-SOC alert threshold in percent, 1..32 (ATHD = 32 - pct). Other CONFIG bits are preserved. */
hal_status_t hal_battery_set_alert_threshold(uint8_t pct);

/** Clear the ALRT line/bit and the STATUS alert flags (keeps the voltage-reset-enable bit). */
hal_status_t hal_battery_clear_alert(void);

/**
 * Quick-start: make the gauge re-estimate SOC from the present voltage (MODE bit 14). Only valid
 * with no load on the cell (voltage must be at rest); otherwise it records a wrong SOC.
 */
hal_status_t hal_battery_quick_start(void);

/**
 * Hibernate thresholds: enter hibernate when |rate| stays below @p hib_mpct_per_h
 * (0.208 %/h steps, max 53040), leave when the cell moves more than @p act_mv (1.25 mV steps,
 * max 318). 0/0 disables hibernate.
 */
hal_status_t hal_battery_set_hibernate(uint32_t hib_mpct_per_h, uint32_t act_mv);

/**
 * Power-on-reset the gauge (CMD 0x5400). The chip resets mid-transaction so the write may be
 * NACKed; that is accepted. Waits 250 ms, then (bounded) until it ACKs again. HAL_OK once it
 * answers. Does not clear STATUS.RI, so the reset stays visible until the next hal_battery_init().
 */
hal_status_t hal_battery_reset(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_BATTERY_H
