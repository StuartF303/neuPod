// SPDX-License-Identifier: MPL-2.0
/**
 * @file max17048.h
 * @brief MAX17048 register map and pure scaling helpers (no I/O; unit-tested on the host).
 *
 * Scaling per the Maxim/Analog Devices MAX17048/MAX17049 datasheet register descriptions:
 * VCELL 78.125 uV/LSB, SOC 1/256 %/LSB, CRATE 0.208 %/h/LSB signed, CONFIG.ATHD = 32 - threshold %,
 * HIBRT hibernate threshold 0.208 %/h/LSB (high byte) and active threshold 1.25 mV/LSB (low byte).
 */
#ifndef NEUPOD_MAX17048_H
#define NEUPOD_MAX17048_H

#include <stdint.h>

#define MAX17048_ADDR 0x36u

#define MAX17048_REG_VCELL 0x02u
#define MAX17048_REG_SOC 0x04u
#define MAX17048_REG_MODE 0x06u
#define MAX17048_REG_VERSION 0x08u
#define MAX17048_REG_HIBRT 0x0Au
#define MAX17048_REG_CONFIG 0x0Cu
#define MAX17048_REG_VALRT 0x14u
#define MAX17048_REG_CRATE 0x16u
#define MAX17048_REG_VRESET_ID 0x18u
#define MAX17048_REG_STATUS 0x1Au
#define MAX17048_REG_CMD 0xFEu

#define MAX17048_MODE_QUICKSTART 0x4000u
#define MAX17048_CMD_POR 0x5400u

#define MAX17048_CONFIG_ATHD_MASK 0x001Fu
#define MAX17048_CONFIG_ALRT 0x0020u
#define MAX17048_CONFIG_ALSC 0x0040u
#define MAX17048_CONFIG_SLEEP 0x0080u

#define MAX17048_STATUS_RI 0x0100u
#define MAX17048_STATUS_VH 0x0200u
#define MAX17048_STATUS_VL 0x0400u
#define MAX17048_STATUS_VR 0x0800u
#define MAX17048_STATUS_HD 0x1000u
#define MAX17048_STATUS_SC 0x2000u
#define MAX17048_STATUS_ENVR 0x4000u
#define MAX17048_STATUS_ALERT_MASK 0x3F00u

#define MAX17048_VERSION_MASK 0xFFF0u
#define MAX17048_VERSION_VALUE 0x0010u /* reads 0x001X */

/** VCELL raw -> mV, rounded to nearest (raw * 78.125 uV = raw * 5/64 mV). */
uint32_t max17048_vcell_to_mv(uint16_t raw);
/** SOC raw (1/256 %) -> whole percent, clamped to 100. */
uint8_t max17048_soc_to_pct(uint16_t raw);
/** CRATE raw (signed) -> milli-percent per hour (raw * 208). */
int32_t max17048_crate_to_mpct_per_h(uint16_t raw);
/** STATUS raw -> HAL_BATTERY_ALERT_* bits. */
uint8_t max17048_status_to_alerts(uint16_t status);
/** CONFIG with ATHD set for an alert at @p pct percent (clamped 1..32). */
uint16_t max17048_config_set_athd(uint16_t config, uint8_t pct);
/** ATHD field -> alert threshold percent (32 - ATHD). */
uint8_t max17048_config_athd_pct(uint16_t config);
/** HIBRT encoding of a hibernate threshold (milli-%/h) and active threshold (mV); clamped to 255 each. */
uint16_t max17048_hibrt_encode(uint32_t hib_mpct_per_h, uint32_t act_mv);

/** HIBRT -> thresholds (milli-%/h, mV rounded down). */
void max17048_hibrt_decode(uint16_t hibrt, uint32_t *hib_mpct_per_h, uint32_t *act_mv);

#endif // NEUPOD_MAX17048_H
