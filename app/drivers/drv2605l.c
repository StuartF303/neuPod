// SPDX-License-Identifier: MPL-2.0
// DRV2605L haptic driver: implements hal_haptic.h on top of hal_i2c. See drv2605l.h for sources.
#include "drv2605l.h"

#include <string.h>

#include "hal_haptic.h"
#include "hal_i2c.h"
#include "hal_system.h"

/*
 * ERM register values (Adafruit 1201 coin motor, rated 3.0 V, ceiling 3.6 V). Default is OPEN loop
 * (NEUPOD_HAPTIC_ERM_LOOP): the closed-loop auto-calibration did not converge on the bench.
 * Open loop (Eq. 6): V = 21.59 mV * OD_CLAMP -> OD = floor(3600 / 21.59) = 166 (0xA6) = 3.584 V.
 * Closed-loop values:
 * Datasheet default timings are used: DRIVE_TIME = 0x13 -> 1 ms + 19 * 0.2 ms = 4.8 ms,
 * BLANKING_TIME = 1 -> 75 us, IDISS_TIME = 1 -> 75 us (SAMPLE_TIME is LRA-only; left at 3).
 *
 *   Eq. 4  V_rated = 21.18 mV * RATED_VOLTAGE       -> floor(3000 / 21.18)             = 141 (0x8D) = 2.986 V
 *   Eq. 8  V_clamp = 21.64 mV * OD * (tDRIVE - 300 us) / (tDRIVE + tIDISS + tBLANK)
 *                  = 21.64 mV * OD * 4500 / 4950    -> floor(3600 / 19.6727)           = 182 (0xB6) = 3.580 V
 *          (183 would give 3.6001 V, over the limit, so the floor matters.)
 */

#define CLICK_DEFAULT_EFFECT 1u /* ROM effect 1 = Strong Click 100 %; placeholder until chosen */
#define RESET_TIMEOUT_MS 100u
#define CAL_POLL_MS 10u
#define DEFAULT_CAL_TIMEOUT_MS 2000u
#define BEMF_GAIN_ERM_DEFAULT 2u /* 1.365x, the power-on default; calibration overwrites it */
#define FB_BRAKE_FACTOR 2u       /* datasheet auto-cal step 3b: "2 is valid for most actuators" */
#define FB_LOOP_GAIN 2u          /* step 3c */
#define AUTO_CAL_TIME_SEL 3u     /* step 3f (1000-1200 ms) */

#ifndef NEUPOD_HAPTIC_LRA_RATED_REG
#define NEUPOD_HAPTIC_LRA_RATED_REG 0x3Eu /* power-on default; needs f_LRA to compute (Eq. 5) */
#endif

static hal_haptic_actuator_t s_act = NEUPOD_HAPTIC_ACTUATOR;
static hal_haptic_loop_t s_loop = NEUPOD_HAPTIC_ERM_LOOP;
static hal_haptic_cal_t s_cal
#if defined(NEUPOD_HAPTIC_CAL_COMP) && defined(NEUPOD_HAPTIC_CAL_BEMF) && defined(NEUPOD_HAPTIC_CAL_BEMF_GAIN)
    = {true, false, NEUPOD_HAPTIC_CAL_COMP, NEUPOD_HAPTIC_CAL_BEMF, NEUPOD_HAPTIC_CAL_BEMF_GAIN}
#endif
    ;
static bool s_inited;
static bool s_click_loaded;
static uint8_t s_click_effect = CLICK_DEFAULT_EFFECT;
static uint32_t s_last_click_us;
static uint32_t s_cal_timeout_ms = DEFAULT_CAL_TIMEOUT_MS;

// --- pure helpers ---------------------------------------------------------------------------

uint32_t drv2605l_erm_drive_time_us(uint8_t reg) { return 1000u + 200u * (reg & 0x1Fu); }

uint32_t drv2605l_erm_dissipation_us(uint8_t sel) {
    static const uint16_t t[4] = {45, 75, 150, 225};
    return t[sel & 3u];
}

uint8_t drv2605l_erm_rated_reg(uint32_t mv) {
    uint32_t r = (uint32_t)((uint64_t)mv * 1000u / 21180u);
    return (uint8_t)(r > 255u ? 255u : r);
}

uint8_t drv2605l_erm_od_clamp_reg(uint32_t mv, uint8_t dt, uint8_t blank_sel, uint8_t idiss_sel) {
    uint64_t td = drv2605l_erm_drive_time_us(dt);
    if (td <= 300u) {
        return 0;
    }
    uint64_t sum = td + drv2605l_erm_dissipation_us(idiss_sel) + drv2605l_erm_dissipation_us(blank_sel);
    uint64_t r = (uint64_t)mv * 1000u * sum / (21640u * (td - 300u));
    return (uint8_t)(r > 255u ? 255u : r);
}

uint8_t drv2605l_erm_ol_od_clamp_reg(uint32_t mv) {
    uint32_t r = (uint32_t)((uint64_t)mv * 1000u / 21590u);
    return (uint8_t)(r > 255u ? 255u : r);
}

uint32_t drv2605l_erm_ol_od_clamp_mv(uint8_t reg) { return ((uint32_t)reg * 21590u + 500u) / 1000u; }

uint8_t drv2605l_lra_od_clamp_reg(uint32_t mv) {
    uint32_t r = (uint32_t)((uint64_t)mv * 1000u / 21220u);
    return (uint8_t)(r > 255u ? 255u : r);
}

uint32_t drv2605l_erm_rated_mv(uint8_t reg) { return ((uint32_t)reg * 21180u + 500u) / 1000u; }

uint32_t drv2605l_erm_od_clamp_mv(uint8_t reg, uint8_t dt, uint8_t blank_sel, uint8_t idiss_sel) {
    uint64_t td = drv2605l_erm_drive_time_us(dt);
    uint64_t sum = td + drv2605l_erm_dissipation_us(idiss_sel) + drv2605l_erm_dissipation_us(blank_sel);
    if (td <= 300u) {
        return 0;
    }
    return (uint32_t)(((uint64_t)reg * 21640u * (td - 300u) / sum + 500u) / 1000u);
}

hal_status_t drv2605l_pack_sequence(const uint8_t *e, size_t count, uint8_t out[8]) {
    if (!e || !out || count == 0 || count > HAL_HAPTIC_SEQ_SLOTS) {
        return HAL_EINVAL;
    }
    memset(out, 0, HAL_HAPTIC_SEQ_SLOTS);
    for (size_t i = 0; i < count; i++) {
        if (e[i] & 0x80u) {
            if ((e[i] & 0x7Fu) == 0) {
                return HAL_EINVAL;
            }
        } else if (e[i] == 0 || e[i] > HAL_HAPTIC_MAX_EFFECT) {
            return HAL_EINVAL;
        }
        out[i] = e[i];
    }
    return HAL_OK;
}

void drv2605l_set_cal_timeout_ms(uint32_t ms) { s_cal_timeout_ms = ms ? ms : DEFAULT_CAL_TIMEOUT_MS; }

// --- bus helpers ----------------------------------------------------------------------------

static hal_status_t map(hal_i2c_err_t e) {
    switch (e) {
    case HAL_I2C_OK: return HAL_OK;
    case HAL_I2C_NACK_ADDR: return HAL_ENODEV;
    case HAL_I2C_TIMEOUT: return HAL_ETIMEOUT;
    case HAL_I2C_BAD_PARAM: return HAL_EINVAL;
    default: return HAL_ERR;
    }
}

static hal_status_t rd(uint8_t reg, uint8_t *v) { return map(hal_i2c_reg_read8(DRV2605L_ADDR, reg, v)); }
static hal_status_t wr(uint8_t reg, uint8_t v) { return map(hal_i2c_reg_write8(DRV2605L_ADDR, reg, v)); }

#define TRY(x)                      \
    do {                            \
        hal_status_t st_ = (x);     \
        if (st_ != HAL_OK) {        \
            return st_;             \
        }                           \
    } while (0)

// Compile-time calibration defaults apply only to the actuator this build targets.
static hal_haptic_cal_t default_cal_for(hal_haptic_actuator_t act) {
    hal_haptic_cal_t c = {0};
#if defined(NEUPOD_HAPTIC_CAL_COMP) && defined(NEUPOD_HAPTIC_CAL_BEMF) && defined(NEUPOD_HAPTIC_CAL_BEMF_GAIN)
    if (act == NEUPOD_HAPTIC_ACTUATOR) {
        c = (hal_haptic_cal_t){true, false, NEUPOD_HAPTIC_CAL_COMP, NEUPOD_HAPTIC_CAL_BEMF,
                               NEUPOD_HAPTIC_CAL_BEMF_GAIN};
    }
#else
    (void)act;
#endif
    return c;
}

// Static actuator configuration (everything the calibration takes as input).
// Order matters for motor safety: the chip powers up in ERM OPEN-LOOP, where OD_CLAMP is the
// full-scale drive (about 21.96 mV x OD, so 182 would be ~4 V). So CONTROL3 is switched to closed
// loop and verified FIRST, and RATED_VOLTAGE / OD_CLAMP are written LAST.
static hal_status_t configure(void) {
    uint8_t rated, od, fb, c3;
    const bool erm_open = s_act == HAL_HAPTIC_ERM && s_loop == HAL_HAPTIC_LOOP_OPEN;
    TRY(rd(DRV_REG_CONTROL3, &c3));
    c3 = erm_open ? (uint8_t)(c3 | DRV_CTRL3_ERM_OPEN_LOOP) : (uint8_t)(c3 & ~DRV_CTRL3_ERM_OPEN_LOOP);
    TRY(wr(DRV_REG_CONTROL3, c3)); // DATA_FORMAT_RTP stays 0 (signed)
    uint8_t c3_back = 0xFF;
    TRY(rd(DRV_REG_CONTROL3, &c3_back));
    if (((c3_back & DRV_CTRL3_ERM_OPEN_LOOP) != 0) != erm_open) {
        return HAL_ERR; // refuse to write the clamp for a loop mode the chip is not in
    }
    if (s_act == HAL_HAPTIC_ERM) {
        // RATED_VOLTAGE is ignored in open loop (datasheet 8.5.2.1: "In open-loop mode, the
        // RATED_VOLTAGE[7:0] bit is ignored"); the closed-loop value is written anyway as a safe one.
        rated = drv2605l_erm_rated_reg(DRV_ERM_RATED_MV);
        // Open loop, Eq. 6: V = 21.59 mV * OD -> OD = floor(3600 / 21.59) = 166 = 3.584 V (full scale).
        // Closed loop, Eq. 8: 182 = 3.580 V (see the block comment at the top of this file).
        od = erm_open ? drv2605l_erm_ol_od_clamp_reg(DRV_ERM_OD_MAX_MV)
                      : drv2605l_erm_od_clamp_reg(DRV_ERM_OD_MAX_MV, DRV_ERM_DRIVE_TIME,
                                                  DRV_ERM_BLANKING_SEL, DRV_ERM_IDISS_SEL);
        fb = (uint8_t)((FB_BRAKE_FACTOR << 4) | (FB_LOOP_GAIN << 2) |
                       (s_cal.valid ? s_cal.bemf_gain : BEMF_GAIN_ERM_DEFAULT));
    } else {
        rated = NEUPOD_HAPTIC_LRA_RATED_REG;
        od = drv2605l_lra_od_clamp_reg(DRV_ERM_OD_MAX_MV); // same 3.6 V ceiling until the LRA is known
        fb = (uint8_t)(DRV_FB_LRA | (FB_BRAKE_FACTOR << 4) | (FB_LOOP_GAIN << 2) |
                       (s_cal.valid ? s_cal.bemf_gain : 2u));
    }
    const uint8_t ctl[3] = {fb, DRV_ERM_CONTROL1, DRV_ERM_CONTROL2};
    TRY(map(hal_i2c_reg_write_burst(DRV2605L_ADDR, DRV_REG_FEEDBACK, ctl, sizeof ctl)));
    TRY(wr(DRV_REG_CONTROL4, (uint8_t)(AUTO_CAL_TIME_SEL << 4)));
    if (s_cal.valid) {
        const uint8_t cal[2] = {s_cal.a_cal_comp, s_cal.a_cal_bemf};
        TRY(map(hal_i2c_reg_write_burst(DRV2605L_ADDR, DRV_REG_A_CAL_COMP, cal, sizeof cal)));
    }
    TRY(wr(DRV_REG_LIBRARY, s_act == HAL_HAPTIC_ERM ? DRV_LIB_ERM_B : DRV_LIB_LRA));
    TRY(wr(DRV_REG_RATED, rated));
    return wr(DRV_REG_OD_CLAMP, od); // last
}

// --- HAL ------------------------------------------------------------------------------------

hal_status_t hal_haptic_safe_state(void) {
    // Best effort, every step attempted: no drive, no playback, standby.
    hal_status_t a = wr(DRV_REG_RTP, 0);
    hal_status_t b = wr(DRV_REG_GO, 0);
    hal_status_t c = wr(DRV_REG_MODE, DRV_MODE_STANDBY);
    s_click_loaded = false;
    return a != HAL_OK ? a : (b != HAL_OK ? b : c);
}

static hal_status_t init_body(void) {
    uint8_t status;
    TRY(rd(DRV_REG_STATUS, &status));
    if (DRV_STATUS_DEVICE_ID(status) != DRV_DEVICE_ID_DRV2605L) {
        return HAL_ENODEV;
    }
    TRY(wr(DRV_REG_MODE, DRV_MODE_DEV_RESET));
    uint32_t t0 = hal_millis();
    bool done = false;
    do {
        hal_delay_ms(1); // give the reset time first; the chip may not ACK meanwhile
        uint8_t mode;
        if (rd(DRV_REG_MODE, &mode) == HAL_OK && !(mode & DRV_MODE_DEV_RESET)) {
            done = true;
        }
    } while (!done && hal_millis() - t0 < RESET_TIMEOUT_MS);
    if (!done) {
        return HAL_ETIMEOUT;
    }
    TRY(wr(DRV_REG_MODE, DRV_MODE_INTERNAL_TRIGGER)); // out of standby
    TRY(configure());
    s_inited = true;
    return hal_haptic_click_prepare(s_click_effect);
}

hal_status_t hal_haptic_init(void) {
    s_inited = false;
    s_click_loaded = false;
    hal_status_t st = init_body();
    if (st != HAL_OK) {
        s_inited = false;
        if (st != HAL_ENODEV) {
            (void)wr(DRV_REG_MODE, DRV_MODE_STANDBY); // never leave a half-configured chip active
        }
    }
    return st;
}

hal_status_t hal_haptic_set_actuator(hal_haptic_actuator_t act) {
    if (act != HAL_HAPTIC_ERM && act != HAL_HAPTIC_LRA) {
        return HAL_EINVAL;
    }
    if (act != s_act) {
        s_act = act;
        s_cal = default_cal_for(act); // calibration belongs to the other actuator
    }
    return s_inited ? hal_haptic_init() : HAL_OK;
}

hal_haptic_actuator_t hal_haptic_get_actuator(void) { return s_act; }

hal_status_t hal_haptic_set_erm_loop(hal_haptic_loop_t loop) {
    if (loop != HAL_HAPTIC_LOOP_OPEN && loop != HAL_HAPTIC_LOOP_CLOSED) {
        return HAL_EINVAL;
    }
    s_loop = loop;
    return s_inited ? hal_haptic_init() : HAL_OK;
}

hal_haptic_loop_t hal_haptic_get_erm_loop(void) { return s_loop; }

hal_status_t hal_haptic_read_info(hal_haptic_info_t *o) {
    if (!o) {
        return HAL_EINVAL;
    }
    uint8_t a[4], b[9];
    TRY(map(hal_i2c_reg_read_burst(DRV2605L_ADDR, DRV_REG_STATUS, a, sizeof a)));
    TRY(map(hal_i2c_reg_read_burst(DRV2605L_ADDR, DRV_REG_RATED, b, sizeof b)));
    o->status = a[0];
    o->mode = a[1];
    o->rtp = a[2];
    o->library = a[3];
    o->rated = b[0];
    o->od_clamp = b[1];
    o->a_cal_comp = b[2];
    o->a_cal_bemf = b[3];
    o->feedback = b[4];
    o->control1 = b[5];
    o->control2 = b[6];
    o->control3 = b[7];
    o->control4 = b[8];
    o->device_id = DRV_STATUS_DEVICE_ID(a[0]);
    return HAL_OK;
}

hal_status_t hal_haptic_set_library(uint8_t lib) {
    if (lib > 7u) {
        return HAL_EINVAL;
    }
    return wr(DRV_REG_LIBRARY, lib);
}

static hal_status_t calibrate_inner(hal_haptic_cal_t *out) {
    // Datasheet 8.5.6 auto-calibration procedure (SLOS854D steps 1-5).
    hal_haptic_cal_t cal = {0};
    TRY(wr(DRV_REG_MODE, DRV_MODE_AUTOCAL)); // step 2: out of standby, auto-cal mode
    s_cal.valid = false;                     // calibrate from the chip's own default BEMF gain
    s_click_loaded = false;
    TRY(configure());                        // step 3: inputs (feedback, rated, OD, times)
    TRY(wr(DRV_REG_GO, 1));                  // step 4
    uint32_t t0 = hal_millis();
    uint8_t go = 1;
    do {
        hal_delay_ms(CAL_POLL_MS);
        TRY(rd(DRV_REG_GO, &go));
    } while ((go & 1u) && hal_millis() - t0 < s_cal_timeout_ms);
    if (go & 1u) {
        return HAL_ETIMEOUT;
    }
    uint8_t status, comp, bemf, fb;
    TRY(rd(DRV_REG_STATUS, &status)); // step 5 (reading clears DIAG_RESULT)
    TRY(rd(DRV_REG_A_CAL_COMP, &comp));
    TRY(rd(DRV_REG_A_CAL_BEMF, &bemf));
    TRY(rd(DRV_REG_FEEDBACK, &fb));
    cal.diag_failed = (status & DRV_STATUS_DIAG_RESULT) != 0;
    cal.a_cal_comp = comp;
    cal.a_cal_bemf = bemf;
    cal.bemf_gain = fb & DRV_FB_BEMF_GAIN_MASK;
    cal.valid = !cal.diag_failed;
    *out = cal;
    return HAL_OK;
}

hal_status_t hal_haptic_calibrate(hal_haptic_cal_t *out) {
    if (!s_inited) {
        return HAL_ERR;
    }
    if (s_act == HAL_HAPTIC_ERM && s_loop == HAL_HAPTIC_LOOP_OPEN) {
        return HAL_ENOTSUP; // auto-calibration only applies to closed-loop operation
    }
    const hal_haptic_cal_t prev = s_cal;
    hal_haptic_cal_t res = {0};
    hal_status_t st = calibrate_inner(&res);
    if (st == HAL_OK && res.valid) {
        s_cal = res; // success: take the new values; chip already holds them
        st = wr(DRV_REG_MODE, DRV_MODE_INTERNAL_TRIGGER);
        if (st == HAL_OK) {
            st = hal_haptic_click_prepare(s_click_effect);
        }
    }
    if (st != HAL_OK || !res.valid) {
        // Single cleanup exit for timeout, I2C error and failed diagnosis: stop, leave auto-cal mode,
        // then rebuild the chip state (reset + configure) with the previous good calibration so the
        // chip's A_CAL_COMP / A_CAL_BEMF / BEMF_GAIN are restored.
        (void)wr(DRV_REG_GO, 0);
        (void)wr(DRV_REG_MODE, DRV_MODE_INTERNAL_TRIGGER);
        s_cal = prev;
        hal_status_t rs = hal_haptic_init();
        if (rs != HAL_OK) {
            (void)wr(DRV_REG_MODE, DRV_MODE_STANDBY);
            s_inited = false;
            if (st == HAL_OK) {
                st = rs;
            }
        }
    }
    if (out) {
        *out = res;
    }
    return st;
}

void hal_haptic_get_calibration(hal_haptic_cal_t *out) {
    if (out) {
        *out = s_cal;
    }
}

hal_status_t hal_haptic_set_calibration(const hal_haptic_cal_t *cal) {
    if (!cal) {
        return HAL_EINVAL;
    }
    s_cal = *cal;
    return HAL_OK;
}

hal_status_t hal_haptic_play(uint8_t effect_id) {
    if (!s_inited) {
        return HAL_ERR;
    }
    if (effect_id == 0 || effect_id > HAL_HAPTIC_MAX_EFFECT) { // a bare id must not be read as a wait entry
        return HAL_EINVAL;
    }
    return hal_haptic_play_sequence(&effect_id, 1);
}

hal_status_t hal_haptic_play_sequence(const uint8_t *entries, size_t count) {
    if (!s_inited) {
        return HAL_ERR;
    }
    uint8_t seq[8];
    TRY(drv2605l_pack_sequence(entries, count, seq));
    s_click_loaded = false;
    TRY(map(hal_i2c_reg_write_burst(DRV2605L_ADDR, DRV_REG_SEQ1, seq, sizeof seq)));
    return wr(DRV_REG_GO, 1);
}

hal_status_t hal_haptic_stop(void) { return wr(DRV_REG_GO, 0); }

hal_status_t hal_haptic_is_playing(bool *playing) {
    uint8_t go;
    TRY(rd(DRV_REG_GO, &go));
    if (playing) {
        *playing = (go & 1u) != 0;
    }
    return HAL_OK;
}

hal_status_t hal_haptic_rtp_begin(void) {
    if (!s_inited) {
        return HAL_ERR;
    }
    s_click_loaded = false;
    TRY(wr(DRV_REG_RTP, 0));
    return wr(DRV_REG_MODE, DRV_MODE_RTP);
}

hal_status_t hal_haptic_rtp_set(int8_t amplitude) { return wr(DRV_REG_RTP, (uint8_t)amplitude); }

hal_status_t hal_haptic_rtp_end(void) {
    TRY(wr(DRV_REG_RTP, 0));
    return wr(DRV_REG_MODE, DRV_MODE_INTERNAL_TRIGGER);
}

hal_status_t hal_haptic_standby(bool standby) {
    hal_status_t st = wr(DRV_REG_MODE, standby ? DRV_MODE_STANDBY : DRV_MODE_INTERNAL_TRIGGER);
    if (st == HAL_OK && !standby) {
        hal_delay_ms(1); // oscillator wake-up margin
    }
    return st;
}

hal_status_t hal_haptic_click_prepare(uint8_t effect_id) {
    if (!s_inited) {
        return HAL_ERR;
    }
    if (effect_id == 0 || effect_id > HAL_HAPTIC_MAX_EFFECT) {
        return HAL_EINVAL;
    }
    const uint8_t entry = effect_id;
    uint8_t seq[8];
    TRY(drv2605l_pack_sequence(&entry, 1, seq));
    TRY(map(hal_i2c_reg_write_burst(DRV2605L_ADDR, DRV_REG_SEQ1, seq, 2))); // slot 1 = effect, slot 2 = 0
    s_click_effect = effect_id;
    s_click_loaded = true;
    return HAL_OK;
}

hal_status_t hal_haptic_click(void) {
    if (!s_inited) {
        return HAL_ERR;
    }
    uint64_t t0 = hal_micros();
    if (!s_click_loaded) {
        TRY(hal_haptic_click_prepare(s_click_effect));
    }
    const uint8_t go[2] = {DRV_REG_GO, 1};
    hal_status_t st = map(hal_i2c_write(DRV2605L_ADDR, go, sizeof go));
    s_last_click_us = (uint32_t)(hal_micros() - t0);
    return st;
}

uint32_t hal_haptic_last_click_us(void) { return s_last_click_us; }
