// SPDX-License-Identifier: MPL-2.0
/**
 * @file drv2605l.h
 * @brief DRV2605L register map and pure calculation helpers (no I/O; unit-tested on the host).
 *
 * Source: TI DRV2605L datasheet SLOS854D (March 2018): register map section 8.6, equations 4, 8
 * and 9 in 8.5.2, ERM timing tables in the Control1/Control2 register descriptions.
 */
#ifndef NEUPOD_DRV2605L_H
#define NEUPOD_DRV2605L_H

#include <stddef.h>
#include <stdint.h>

#include "hal_types.h"

#define DRV2605L_ADDR 0x5Au

#define DRV_REG_STATUS 0x00u
#define DRV_REG_MODE 0x01u
#define DRV_REG_RTP 0x02u
#define DRV_REG_LIBRARY 0x03u
#define DRV_REG_SEQ1 0x04u /* 0x04..0x0B */
#define DRV_REG_GO 0x0Cu
#define DRV_REG_RATED 0x16u
#define DRV_REG_OD_CLAMP 0x17u
#define DRV_REG_A_CAL_COMP 0x18u
#define DRV_REG_A_CAL_BEMF 0x19u
#define DRV_REG_FEEDBACK 0x1Au
#define DRV_REG_CONTROL1 0x1Bu
#define DRV_REG_CONTROL2 0x1Cu
#define DRV_REG_CONTROL3 0x1Du
#define DRV_REG_CONTROL4 0x1Eu

#define DRV_STATUS_DIAG_RESULT 0x08u
#define DRV_STATUS_DEVICE_ID(s) ((uint8_t)((s) >> 5))
#define DRV_DEVICE_ID_DRV2605L 7u

#define DRV_MODE_DEV_RESET 0x80u
#define DRV_MODE_STANDBY 0x40u
#define DRV_MODE_INTERNAL_TRIGGER 0x00u
#define DRV_MODE_RTP 0x05u
#define DRV_MODE_AUTOCAL 0x07u

#define DRV_FB_LRA 0x80u
#define DRV_FB_BEMF_GAIN_MASK 0x03u
#define DRV_CTRL3_ERM_OPEN_LOOP 0x20u

#define DRV_LIB_ERM_B 2u
#define DRV_LIB_LRA 6u

/** Motor facts (Adafruit 1201 coin ERM): rated 3.0 V, never exceed 3.6 V. */
#define DRV_ERM_RATED_MV 3000u
#define DRV_ERM_OD_MAX_MV 3600u

/* ERM datasheet-default timings (register defaults): CONTROL1 = 0x93 (STARTUP_BOOST, DRIVE_TIME
 * 0x13), CONTROL2 = 0xF5 (BIDIR_INPUT, BRAKE_STABILIZER, SAMPLE_TIME 3, BLANKING 1, IDISS 1). */
#define DRV_ERM_DRIVE_TIME 0x13u
#define DRV_ERM_CONTROL1 (0x80u | DRV_ERM_DRIVE_TIME)
#define DRV_ERM_BLANKING_SEL 1u
#define DRV_ERM_IDISS_SEL 1u
#define DRV_ERM_CONTROL2 0xF5u

/** ERM drive time in us = 1000 + 200 * DRIVE_TIME[4:0]. */
uint32_t drv2605l_erm_drive_time_us(uint8_t drive_time_reg);
/** ERM blanking / current-dissipation time in us for a 2-bit selector: 45, 75, 150, 225. */
uint32_t drv2605l_erm_dissipation_us(uint8_t sel);
/** Eq. 4: RATED_VOLTAGE = floor(V / 21.18 mV) (ERM closed loop, average voltage), clamped to 255. */
uint8_t drv2605l_erm_rated_reg(uint32_t mv);
/**
 * Eq. 8 inverted: V_clamp = 21.64 mV * OD * (tDRIVE - 300 us) / (tDRIVE + tIDISS + tBLANK),
 * so OD = floor(V * (tDRIVE + tIDISS + tBLANK) / (21.64 mV * (tDRIVE - 300 us))); the floor makes
 * the real clamp never exceed @p mv. Clamped to 255; 0 if tDRIVE <= 300 us.
 */
uint8_t drv2605l_erm_od_clamp_reg(uint32_t mv, uint8_t drive_time_reg, uint8_t blanking_sel,
                                  uint8_t idiss_sel);
/**
 * Eq. 6 inverted (ERM OPEN loop): V = 21.59 mV * OD_CLAMP (full-scale drive, no overdrive),
 * so OD = floor(V / 21.59 mV), clamped to 255. 3600 mV gives 166 (3.584 V; 167 would be 3.605 V).
 */
uint8_t drv2605l_erm_ol_od_clamp_reg(uint32_t mv);
/** Eq. 6: voltage (mV, rounded) for an open-loop OD_CLAMP value. */
uint32_t drv2605l_erm_ol_od_clamp_mv(uint8_t reg);
/** Eq. 9: LRA clamp, OD = floor(V / 21.22 mV). */
uint8_t drv2605l_lra_od_clamp_reg(uint32_t mv);
/** Voltage (mV, rounded) represented by a RATED_VOLTAGE register value (Eq. 4). */
uint32_t drv2605l_erm_rated_mv(uint8_t reg);
/** Peak clamp voltage (mV, rounded) for an OD_CLAMP value and ERM timings (Eq. 8). */
uint32_t drv2605l_erm_od_clamp_mv(uint8_t reg, uint8_t drive_time_reg, uint8_t blanking_sel,
                                  uint8_t idiss_sel);

/**
 * Pack sequencer entries for registers 0x04..0x0B. Entry = effect id 1..123, or a wait
 * (bit 7 set, delay 10 ms * (entry & 0x7F), 1..127). Unused slots are 0 (terminator).
 * HAL_EINVAL for count 0 or > 8, id 0/124..127, or a zero-length wait.
 */
hal_status_t drv2605l_pack_sequence(const uint8_t *entries, size_t count, uint8_t out[8]);

/** Test hook: calibration GO-poll timeout (default 2000 ms; datasheet maximum cal time is 1200 ms). */
void drv2605l_set_cal_timeout_ms(uint32_t ms);

#endif // NEUPOD_DRV2605L_H
