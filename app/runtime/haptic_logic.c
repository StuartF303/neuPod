// SPDX-License-Identifier: MPL-2.0
#include "haptic_logic.h"

#define CAL_VERSION 1u

bool click_mode_from_stored(uint8_t v, click_mode_t *out) {
    if (v > CLICK_BOTH) {
        return false;
    }
    *out = (click_mode_t)v;
    return true;
}

click_mode_t click_mode_next(click_mode_t m) {
    switch (m) {
    case CLICK_PIEZO: return CLICK_HAPTIC;
    case CLICK_HAPTIC: return CLICK_BOTH;
    case CLICK_BOTH: return CLICK_OFF;
    default: return CLICK_PIEZO;
    }
}

const char *click_mode_name(click_mode_t m) {
    switch (m) {
    case CLICK_PIEZO: return "Piezo";
    case CLICK_HAPTIC: return "Haptic";
    case CLICK_BOTH: return "Both";
    default: return "Off";
    }
}

bool click_mode_uses_haptic(click_mode_t m) { return m == CLICK_HAPTIC || m == CLICK_BOTH; }
bool click_mode_uses_piezo(click_mode_t m) { return m == CLICK_PIEZO || m == CLICK_BOTH; }

bool haptic_gate_ready(const haptic_gate_t *g, uint32_t now_ms, uint32_t min_gap_ms) {
    return !g->have || (uint32_t)(now_ms - g->last_ms) >= min_gap_ms;
}

void haptic_gate_mark(haptic_gate_t *g, uint32_t now_ms) {
    g->have = true;
    g->last_ms = now_ms;
}

bool haptic_gate_pass(haptic_gate_t *g, uint32_t now_ms, uint32_t min_gap_ms) {
    if (!haptic_gate_ready(g, now_ms, min_gap_ms)) {
        return false;
    }
    haptic_gate_mark(g, now_ms);
    return true;
}

bool haptic_click_admissible(bool calibrating) { return !calibrating; }

void haptic_cal_encode(hal_haptic_actuator_t act, const hal_haptic_lra_params_t *lra, const hal_haptic_cal_t *cal,
                       uint8_t out[HAPTIC_CAL_RECORD_LEN]) {
    // The LRA parameters only matter to an LRA record; an ERM record stores zeros so they never mismatch.
    const uint16_t mv = act == HAL_HAPTIC_LRA ? lra->rated_mv_rms : 0u;
    const uint16_t hz = act == HAL_HAPTIC_LRA ? lra->freq_hz : 0u;
    out[0] = CAL_VERSION;
    out[1] = (uint8_t)act;
    out[2] = cal->a_cal_comp;
    out[3] = cal->a_cal_bemf;
    out[4] = cal->bemf_gain;
    out[5] = (uint8_t)(mv & 0xFFu);
    out[6] = (uint8_t)(mv >> 8);
    out[7] = (uint8_t)(hz & 0xFFu);
    out[8] = (uint8_t)(hz >> 8);
}

bool haptic_cal_decode(const uint8_t *rec, size_t len, hal_haptic_actuator_t act, const hal_haptic_lra_params_t *lra,
                       hal_haptic_cal_t *cal) {
    if (rec == NULL || len != HAPTIC_CAL_RECORD_LEN || rec[0] != CAL_VERSION || rec[1] != (uint8_t)act || rec[4] > 3u) {
        return false;
    }
    const uint16_t mv = (uint16_t)(rec[5] | (rec[6] << 8));
    const uint16_t hz = (uint16_t)(rec[7] | (rec[8] << 8));
    if (act == HAL_HAPTIC_LRA && (mv != lra->rated_mv_rms || hz != lra->freq_hz)) {
        return false;
    }
    *cal = (hal_haptic_cal_t){.valid = true, .diag_failed = false, .a_cal_comp = rec[2], .a_cal_bemf = rec[3], .bemf_gain = rec[4]};
    return true;
}
