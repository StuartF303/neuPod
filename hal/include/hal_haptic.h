// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_haptic.h
 * @brief Haptic driver (DRV2605L at I2C 0x5A), portable over hal_i2c.
 *
 * Implemented in app/drivers/drv2605l.c. The I2C bus must already be initialised. Status codes:
 * HAL_OK, HAL_ENODEV (no ACK / wrong chip), HAL_ETIMEOUT, HAL_EINVAL, HAL_ENOTSUP, HAL_ERR.
 *
 * Actuator: build-time default NEUPOD_HAPTIC_ACTUATOR (HAL_HAPTIC_ERM unless overridden with
 * -DNEUPOD_HAPTIC_ACTUATOR=HAL_HAPTIC_LRA), changeable at run time with hal_haptic_set_actuator().
 * ERM uses ROM library 2 (TS2200 "B"), open loop by default; LRA uses library 6, closed loop (auto-resonance),
 * with RATED_VOLTAGE / OD_CLAMP / DRIVE_TIME computed from hal_haptic_lra_params_t.
 */
#ifndef NEUPOD_HAL_HAPTIC_H
#define NEUPOD_HAL_HAPTIC_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HAL_HAPTIC_ERM = 0, HAL_HAPTIC_LRA = 1 } hal_haptic_actuator_t;

/**
 * LRA actuator parameters (the inputs to the datasheet equations): rated voltage in mV RMS (Eq. 5 -> RATED_VOLTAGE),
 * the largest peak voltage allowed in mV (Eq. 9 -> OD_CLAMP) and the resonant frequency in Hz (DRIVE_TIME, Eq. 5).
 */
typedef struct {
    uint16_t rated_mv_rms;
    uint16_t peak_mv;
    uint16_t freq_hz;
} hal_haptic_lra_params_t;

/** LEADER LD0832AA (8 mm coin LRA): 2 V rms rated, 235 Hz; the peak ceiling is 2 V x sqrt(2), floored to 2.8 V. */
#define HAL_HAPTIC_LRA_LD0832AA {2000u, 2800u, 235u}

/** ERM drive mode: open loop (OD_CLAMP is the full-scale voltage) or closed loop (needs auto-cal). */
typedef enum { HAL_HAPTIC_LOOP_OPEN = 0, HAL_HAPTIC_LOOP_CLOSED = 1 } hal_haptic_loop_t;

/** Build-time ERM loop mode: -DNEUPOD_HAPTIC_ERM_LOOP=HAL_HAPTIC_LOOP_CLOSED. Default OPEN. */
#ifndef NEUPOD_HAPTIC_ERM_LOOP
#define NEUPOD_HAPTIC_ERM_LOOP HAL_HAPTIC_LOOP_OPEN
#endif

#ifndef NEUPOD_HAPTIC_ACTUATOR
#define NEUPOD_HAPTIC_ACTUATOR HAL_HAPTIC_ERM
#endif

/** ROM effect ids are 1..HAL_HAPTIC_MAX_EFFECT; sequencer holds up to HAL_HAPTIC_SEQ_SLOTS entries. */
#define HAL_HAPTIC_MAX_EFFECT 123u
#define HAL_HAPTIC_SEQ_SLOTS 8u
/** Sequence entry meaning "wait @p ms" (10 ms steps, 10..1270 ms): HAL_HAPTIC_WAIT_MS(100). */
#define HAL_HAPTIC_WAIT_MS(ms) ((uint8_t)(0x80u | ((ms) / 10u)))

/** Auto-calibration results (also what gets re-applied after init). */
typedef struct {
    bool valid;          /**< Values below are usable (from calibration or build-time defaults). */
    bool diag_failed;    /**< DIAG_RESULT was 1: calibration did not converge. */
    uint8_t a_cal_comp;  /**< 0x18 compensation: coefficient = 1 + a_cal_comp/255. */
    uint8_t a_cal_bemf;  /**< 0x19 rated back-EMF. */
    uint8_t bemf_gain;   /**< 0x1A bits 1:0 chosen by the calibration. */
} hal_haptic_cal_t;

/** Snapshot of the chip registers (read in two bursts). */
typedef struct {
    uint8_t status, mode, rtp, library;                 /* 0x00..0x03 */
    uint8_t rated, od_clamp, a_cal_comp, a_cal_bemf;    /* 0x16..0x19 */
    uint8_t feedback, control1, control2, control3, control4; /* 0x1A..0x1E */
    uint8_t device_id;                                  /**< status bits 7:5 (7 = DRV2605L). */
} hal_haptic_info_t;

/**
 * Best-effort motor safe state, independent of init: RTP_INPUT = 0, GO = 0, MODE = standby. Every
 * step is attempted; returns the first error. Use at start-up and on error paths.
 */
hal_status_t hal_haptic_safe_state(void);

/**
 * Bring-up order is motor-safe: leave standby, set CONTROL3 to the chosen loop mode and verify it, write
 * feedback/timing, then RATED_VOLTAGE and OD_CLAMP last (the chip powers up open-loop, where the
 * clamp is the full-scale drive; the clamp is always written for the loop mode already in effect). On any failure the chip is put back into standby and the
 * driver stays uninitialised: play/sequence/click/RTP/calibrate then return HAL_ERR.
 *
 * Reset the chip, leave standby, internal-trigger mode, configure the current actuator, apply
 * stored calibration, preload the click effect. HAL_ENODEV if nothing ACKs or DEVICE_ID != 7.
 */
hal_status_t hal_haptic_init(void);

/**
 * Switch actuator type at run time; re-initialises if already initialised. Calibration is cleared
 * only when the type actually changes (then the build-time defaults apply if they are for the new type).
 */
hal_status_t hal_haptic_set_actuator(hal_haptic_actuator_t act);
hal_haptic_actuator_t hal_haptic_get_actuator(void);

/**
 * Set the LRA parameters used by the next init (re-initialises if already initialised and the actuator is an LRA).
 * HAL_EINVAL (nothing changed) unless 500 <= rated <= peak <= 3600 mV and 50 <= freq <= 500 Hz. Stored calibration
 * belongs to the old parameters, so it is cleared when they change.
 */
hal_status_t hal_haptic_set_lra_params(const hal_haptic_lra_params_t *p);
void hal_haptic_get_lra_params(hal_haptic_lra_params_t *out);

/**
 * Select ERM open/closed loop at run time (re-initialises if already initialised). OPEN: CONTROL3
 * ERM_OPEN_LOOP = 1, OD_CLAMP = 166 (21.59 mV per step, 3.584 V). CLOSED: bit clear, OD_CLAMP = 182
 * (3.580 V). Ignored for LRA.
 */
hal_status_t hal_haptic_set_erm_loop(hal_haptic_loop_t loop);
hal_haptic_loop_t hal_haptic_get_erm_loop(void);

/**
 * Read all interesting registers. Note: reading STATUS clears its DIAG_RESULT flag, so after a
 * calibration use the result struct, not this.
 */
hal_status_t hal_haptic_read_info(hal_haptic_info_t *out);

/** Select ROM library: 1-5 and 7 = ERM (A-E, F), 6 = LRA. */
hal_status_t hal_haptic_set_library(uint8_t lib);

/**
 * Run the datasheet auto-calibration for the current actuator (the motor spins ~1 s). Closed loop
 * only: HAL_ENOTSUP for ERM open loop. out receives the values measured right after GO cleared,
 * BEFORE any failure cleanup/restore. Fills
 * @p out and stores the result in RAM (applied by init). HAL_OK even when the chip reports a
 * failed calibration: check out->diag_failed. On any failure (timeout, I2C error, failed diagnosis)
 * the chip is rebuilt from reset with the previous good calibration. HAL_ERR if not initialised.
 */
hal_status_t hal_haptic_calibrate(hal_haptic_cal_t *out);

/** Stored calibration (RAM; initialised from NEUPOD_HAPTIC_CAL_* build defaults if all given). */
void hal_haptic_get_calibration(hal_haptic_cal_t *out);
hal_status_t hal_haptic_set_calibration(const hal_haptic_cal_t *cal);

/** Play one ROM effect (1..123) and return immediately. */
hal_status_t hal_haptic_play(uint8_t effect_id);

/**
 * Play up to 8 sequence entries: an effect id (1..123) or HAL_HAPTIC_WAIT_MS(n). Unused slots are
 * terminated with 0. Returns immediately.
 */
hal_status_t hal_haptic_play_sequence(const uint8_t *entries, size_t count);

/** Cancel playback (clears GO). */
hal_status_t hal_haptic_stop(void);

/** True while a waveform is playing (GO set). */
hal_status_t hal_haptic_is_playing(bool *playing);

/**
 * Real-time playback: hal_haptic_rtp_begin() selects RTP mode; hal_haptic_rtp_set() writes the
 * amplitude. Format is SIGNED (DATA_FORMAT_RTP = 0, the chip default) with the default
 * input. Closed loop: 0 = no drive, 127 = full rated voltage, 63 = about half; negative values
 * behave as 0. ERM OPEN loop (datasheet 8.5.8.1.1, signed): 0 = no drive, 127 = +OD_CLAMP, negative
 * values drive in reverse (braking), -127 = -OD_CLAMP. Callers that only ramp 0..127 are safe in both. hal_haptic_rtp_end() writes 0 and returns to
 * internal-trigger mode.
 */
hal_status_t hal_haptic_rtp_begin(void);
hal_status_t hal_haptic_rtp_set(int8_t amplitude);
hal_status_t hal_haptic_rtp_end(void);

/** Standby (low power, registers kept) / wake (re-selects internal trigger). */
hal_status_t hal_haptic_standby(bool standby);

/**
 * Lowest-latency click: hal_haptic_click_prepare() loads effect @p effect_id into sequencer slot 1
 * (slot 2 = 0). hal_haptic_click() then does ONE two-byte I2C write (GO = 1). If another call
 * overwrote the sequencer since, click() reloads it first (slower path, flagged by
 * hal_haptic_last_click_us() including that cost).
 */
hal_status_t hal_haptic_click_prepare(uint8_t effect_id);
hal_status_t hal_haptic_click(void);

/**
 * Microseconds from just before the GO write was issued to its completion (ACK + STOP) in the last
 * click(). This is host-visible I2C latency only; the motor's electrical/mechanical response is
 * not measured (no current sense or scope hook on this board).
 */
uint32_t hal_haptic_last_click_us(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_HAPTIC_H
