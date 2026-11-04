// SPDX-License-Identifier: MPL-2.0
/**
 * @file haptic_logic.h
 * @brief Pure click-feedback logic (no OS, no I/O; unit-tested on the host): the Click setting, the click
 *        rate gate, and the stored DRV2605L calibration record.
 */
#ifndef NEUPOD_HAPTIC_LOGIC_H
#define NEUPOD_HAPTIC_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Settings > Click. The numeric values are what SETTING_CLICK_FEEDBACK stores (0 = off and 1 = haptic match the old on/off). */
typedef enum { CLICK_OFF = 0, CLICK_HAPTIC = 1, CLICK_PIEZO = 2, CLICK_BOTH = 3 } click_mode_t;

/** False (and @p out untouched) for a stored value that is not a mode. */
bool click_mode_from_stored(uint8_t v, click_mode_t *out);
/** Menu order: Piezo, Haptic, Both, Off, then round again. */
click_mode_t click_mode_next(click_mode_t m);
const char *click_mode_name(click_mode_t m);
bool click_mode_uses_haptic(click_mode_t m);
bool click_mode_uses_piezo(click_mode_t m);

/** Click rate gate: accepts a request unless the previous accepted one was less than @p min_gap_ms ago. */
typedef struct {
    uint32_t last_ms;
    bool have;
} haptic_gate_t;
bool haptic_gate_pass(haptic_gate_t *g, uint32_t now_ms, uint32_t min_gap_ms);

/**
 * Stored calibration record (settings tag SETTING_HAPTIC_CAL): version, actuator, A_CAL_COMP, A_CAL_BEMF,
 * BEMF_GAIN, and the actuator parameters it was measured with (rated mV rms, resonance Hz; little endian).
 * It is only used again for the same actuator type and the same parameters, so changing either recalibrates.
 */
#define HAPTIC_CAL_RECORD_LEN 9u
void haptic_cal_encode(hal_haptic_actuator_t act, const hal_haptic_lra_params_t *lra, const hal_haptic_cal_t *cal,
                       uint8_t out[HAPTIC_CAL_RECORD_LEN]);
/** True and @p cal filled (valid = true) when the record is well formed and belongs to @p act / @p lra. */
bool haptic_cal_decode(const uint8_t *rec, size_t len, hal_haptic_actuator_t act, const hal_haptic_lra_params_t *lra,
                       hal_haptic_cal_t *cal);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAPTIC_LOGIC_H
