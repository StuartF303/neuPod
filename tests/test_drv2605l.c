// SPDX-License-Identifier: MPL-2.0
// DRV2605L driver: calculation helpers, sequencer packing, init/calibration/click flows against a
// fake chip that clears GO.
#include <stdio.h>
#include <string.h>

#include "drv2605l.h"
#include "hal_haptic.h"
#include "hal_i2c.h"
#include "hal_i2c_mock.h"

static int s_fail;
#define CHECK(c)                                                                  \
    do {                                                                          \
        if (!(c)) {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                   \
            s_fail++;                                                             \
        }                                                                         \
    } while (0)

static uint8_t regs[0x80];
static unsigned s_go_writes;
static bool s_cal_fail, s_cal_hang;
static uint8_t s_cal_comp_result = 0x0D;
static bool s_inject_on_go, s_inject_on_c3;
static int s_viol;                      // safety invariant violations seen at write time
static unsigned s_max_rated, s_max_od;  // largest values ever written, per actuator in use
static unsigned s_max_od_lra, s_max_od_ol;

static void on_w(void *ctx, uint8_t reg, uint8_t v) {
    (void)ctx;
    bool lra = regs[DRV_REG_FEEDBACK] & DRV_FB_LRA;
    if (reg == DRV_REG_MODE && (v & DRV_MODE_DEV_RESET)) { // reset: defaults, bit self-clears
        regs[DRV_REG_MODE] = DRV_MODE_STANDBY;
        regs[DRV_REG_CONTROL3] = 0xA0; // ERM_OPEN_LOOP = 1 at power-on
        regs[DRV_REG_LIBRARY] = 1;
        regs[DRV_REG_RATED] = 0x3E;
        regs[DRV_REG_OD_CLAMP] = 0x8C;
        regs[DRV_REG_FEEDBACK] = 0x36;
    } else if (reg == DRV_REG_RATED || reg == DRV_REG_OD_CLAMP) {
        if (reg == DRV_REG_RATED && !lra && v > s_max_rated) {
            s_max_rated = v;
        }
        if (reg == DRV_REG_OD_CLAMP) {
            bool ol = regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP;
            if (lra) {
                s_max_od_lra = v > s_max_od_lra ? v : s_max_od_lra;
            } else if (ol) {
                s_max_od_ol = v > s_max_od_ol ? v : s_max_od_ol;
                if (v > 166) {
                    s_viol++; // open-loop clamp is the full-scale voltage: never above 3.6 V
                }
            } else {
                s_max_od = v > s_max_od ? v : s_max_od;
                if (v > 182) {
                    s_viol++;
                }
            }
        }
    } else if (reg == DRV_REG_CONTROL3) {
        if ((v & DRV_CTRL3_ERM_OPEN_LOOP) && regs[DRV_REG_OD_CLAMP] > 166 && !(regs[DRV_REG_FEEDBACK] & DRV_FB_LRA)) {
            s_viol++; // switching to open loop while a closed-loop-sized clamp is in place
        }
        if (s_inject_on_c3) { // make the verification read-back fail
            s_inject_on_c3 = false;
            hal_i2c_mock_inject_error(HAL_I2C_TIMEOUT, 1);
        }
    } else if (reg == DRV_REG_GO && (v & 1)) {
        s_go_writes++;
        if ((regs[DRV_REG_MODE] & 7) == DRV_MODE_AUTOCAL && !s_cal_hang) {
            regs[DRV_REG_GO] = 0;
            regs[DRV_REG_A_CAL_COMP] = s_cal_comp_result;
            regs[DRV_REG_A_CAL_BEMF] = 0x7B;
            regs[DRV_REG_FEEDBACK] = (uint8_t)((regs[DRV_REG_FEEDBACK] & ~3u) | 1u);
            regs[DRV_REG_STATUS] = (uint8_t)(0xE0 | (s_cal_fail ? DRV_STATUS_DIAG_RESULT : 0));
            if (s_inject_on_go) {
                s_inject_on_go = false;
                hal_i2c_mock_inject_error(HAL_I2C_TIMEOUT, 1); // next transaction (the GO poll) fails
            }
        }
    }
}

int main(void) {
    // --- calculations (datasheet Eq. 4, 8, 9 with ERM default timings) ---
    CHECK(drv2605l_erm_drive_time_us(0x13) == 4800);
    CHECK(drv2605l_erm_dissipation_us(1) == 75 && drv2605l_erm_dissipation_us(3) == 225);
    CHECK(drv2605l_erm_rated_reg(3000) == 141);            // 3.0 V / 21.18 mV = 141.6 -> 141
    CHECK(drv2605l_erm_rated_mv(141) == 2986);
    CHECK(drv2605l_erm_rated_reg(0) == 0);
    CHECK(drv2605l_erm_rated_reg(100000) == 255);          // clamped
    CHECK(drv2605l_erm_od_clamp_reg(3600, 0x13, 1, 1) == 182);
    CHECK(drv2605l_erm_od_clamp_mv(182, 0x13, 1, 1) == 3580);
    // the next code up would exceed 3.6 V: 183 * 21.64 mV * 4500/4950 = 3600.1 mV
    CHECK(183u * 21640u * 4500u > 3600000u * 4950u);
    CHECK(drv2605l_lra_od_clamp_reg(3600) == 169);
    CHECK(drv2605l_erm_ol_od_clamp_reg(3600) == 166 && drv2605l_erm_ol_od_clamp_mv(166) == 3584);
    CHECK(167u * 21590u > 3600000u);                       // 167 would exceed 3.6 V
    CHECK(drv2605l_erm_ol_od_clamp_reg(100000) == 255);

    // --- sequencer packing ---
    uint8_t out[8];
    const uint8_t s1[] = {1, HAL_HAPTIC_WAIT_MS(100), 2};
    CHECK(HAL_HAPTIC_WAIT_MS(100) == 0x8A);
    CHECK(drv2605l_pack_sequence(s1, 3, out) == HAL_OK);
    const uint8_t e1[8] = {1, 0x8A, 2, 0, 0, 0, 0, 0};
    CHECK(memcmp(out, e1, 8) == 0);
    const uint8_t s8[8] = {1, 2, 3, 4, 5, 6, 7, 123};
    CHECK(drv2605l_pack_sequence(s8, 8, out) == HAL_OK && out[7] == 123);
    const uint8_t s9[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
    CHECK(drv2605l_pack_sequence(s9, 9, out) == HAL_EINVAL);
    const uint8_t bad0[] = {0}, bad124[] = {124}, bad_wait[] = {0x80}, wait_max[] = {0xFF};
    CHECK(drv2605l_pack_sequence(bad0, 1, out) == HAL_EINVAL);
    CHECK(drv2605l_pack_sequence(bad124, 1, out) == HAL_EINVAL);
    CHECK(drv2605l_pack_sequence(bad_wait, 1, out) == HAL_EINVAL);
    CHECK(drv2605l_pack_sequence(wait_max, 1, out) == HAL_OK && out[0] == 0xFF);
    CHECK(drv2605l_pack_sequence(s1, 0, out) == HAL_EINVAL);

    // --- driver against the fake chip ---
    hal_i2c_mock_reset();
    CHECK(hal_i2c_init(400000) == HAL_I2C_OK);
    CHECK(hal_haptic_init() == HAL_ENODEV);                // nothing at 0x5A
    CHECK(hal_i2c_mock_add_regmap(0x5A, regs, sizeof regs, on_w, NULL));
    regs[0] = 0x60;                                        // DRV2605 (id 3), not an L part
    CHECK(hal_haptic_init() == HAL_ENODEV);
    CHECK(hal_haptic_play(1) != HAL_OK && hal_haptic_click() != HAL_OK && hal_haptic_rtp_begin() != HAL_OK);
    CHECK(hal_haptic_calibrate(NULL) != HAL_OK && hal_haptic_play_sequence(s1, 3) != HAL_OK);
    regs[0] = 0xE0;
    CHECK(hal_haptic_get_actuator() == HAL_HAPTIC_ERM);
    CHECK(hal_haptic_get_erm_loop() == HAL_HAPTIC_LOOP_OPEN); // build default
    CHECK(hal_haptic_set_erm_loop(HAL_HAPTIC_LOOP_CLOSED) == HAL_OK); // the flows below are closed-loop
    CHECK(hal_haptic_play(1) != HAL_OK);                   // still not initialised
    // init failing half-way (CONTROL3 read-back times out): chip left in standby, driver unusable
    s_inject_on_c3 = true;
    CHECK(hal_haptic_init() != HAL_OK);
    CHECK(regs[DRV_REG_MODE] == DRV_MODE_STANDBY && hal_haptic_play(1) != HAL_OK);
    CHECK(hal_haptic_init() == HAL_OK);
    CHECK(regs[DRV_REG_MODE] == 0x00);                     // out of standby, internal trigger
    CHECK(regs[DRV_REG_RATED] == 141 && regs[DRV_REG_OD_CLAMP] == 182);
    CHECK(regs[DRV_REG_FEEDBACK] == 0x2A);                 // ERM, brake 2, loop gain 2, bemf gain 2
    CHECK(regs[DRV_REG_CONTROL1] == 0x93 && regs[DRV_REG_CONTROL2] == 0xF5);
    CHECK(regs[DRV_REG_CONTROL3] == 0x80);                 // ERM_OPEN_LOOP cleared, signed RTP
    CHECK(regs[DRV_REG_CONTROL4] == 0x30);                 // AUTO_CAL_TIME = 3
    CHECK(regs[DRV_REG_LIBRARY] == 2);
    CHECK(regs[DRV_REG_SEQ1] == 1 && regs[DRV_REG_SEQ1 + 1] == 0);

    hal_haptic_info_t info;
    CHECK(hal_haptic_read_info(&info) == HAL_OK && info.device_id == 7 && info.rated == 141 &&
          info.library == 2 && info.control4 == 0x30);

    // calibration flow: GO clears, results read back, stored
    hal_haptic_cal_t cal;
    CHECK(s_max_od == 182 && s_viol == 0);
    CHECK(hal_haptic_calibrate(&cal) == HAL_OK);
    CHECK(cal.valid && !cal.diag_failed && cal.a_cal_comp == 0x0D && cal.a_cal_bemf == 0x7B &&
          cal.bemf_gain == 1);
    CHECK(regs[DRV_REG_MODE] == 0x00);                     // back to internal trigger
    hal_haptic_cal_t stored;
    hal_haptic_get_calibration(&stored);
    CHECK(stored.valid && stored.a_cal_bemf == 0x7B);
    CHECK(hal_haptic_init() == HAL_OK);                    // re-applies the stored values
    CHECK(regs[DRV_REG_A_CAL_COMP] == 0x0D && regs[DRV_REG_A_CAL_BEMF] == 0x7B);
    CHECK(regs[DRV_REG_FEEDBACK] == 0x29);

    // failed calibration keeps the earlier good one, and the chip registers are restored
    s_cal_fail = true;
    s_cal_comp_result = 0x55;
    CHECK(hal_haptic_calibrate(&cal) == HAL_OK && cal.diag_failed && !cal.valid);
    CHECK(cal.a_cal_comp == 0x55 && cal.a_cal_bemf == 0x7B); // measured values reported, not the restored ones
    hal_haptic_get_calibration(&stored);
    CHECK(stored.valid && stored.a_cal_bemf == 0x7B && stored.a_cal_comp == 0x0D);
    CHECK(regs[DRV_REG_A_CAL_COMP] == 0x0D && regs[DRV_REG_MODE] == 0x00);
    s_cal_fail = false;

    // I2C error in the middle of calibration: cleanup leaves a sane, restored chip
    s_inject_on_go = true;
    CHECK(hal_haptic_calibrate(&cal) != HAL_OK);
    CHECK(regs[DRV_REG_MODE] == 0x00 && regs[DRV_REG_GO] == 0);
    CHECK(regs[DRV_REG_A_CAL_COMP] == 0x0D && (regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP) == 0);
    CHECK(hal_haptic_play(1) == HAL_OK);                   // driver is usable again
    s_cal_comp_result = 0x0D;

    // GO never clears: times out, cancels, returns to internal trigger
    s_cal_hang = true;
    drv2605l_set_cal_timeout_ms(50);
    CHECK(hal_haptic_calibrate(&cal) == HAL_ETIMEOUT);
    CHECK(regs[DRV_REG_GO] == 0 && regs[DRV_REG_MODE] == 0x00);
    CHECK(regs[DRV_REG_A_CAL_COMP] == 0x0D);               // restored after the timeout too
    s_cal_hang = false;
    drv2605l_set_cal_timeout_ms(0);

    // sequences
    s_go_writes = 0;
    CHECK(hal_haptic_play_sequence(s1, 3) == HAL_OK);
    CHECK(memcmp(&regs[DRV_REG_SEQ1], e1, 8) == 0 && s_go_writes == 1);
    CHECK(hal_haptic_play(200) == HAL_EINVAL);
    CHECK(hal_haptic_play(7) == HAL_OK && regs[DRV_REG_SEQ1] == 7 && regs[DRV_REG_SEQ1 + 1] == 0);
    bool playing = false;
    CHECK(hal_haptic_is_playing(&playing) == HAL_OK && playing);
    CHECK(hal_haptic_stop() == HAL_OK && regs[DRV_REG_GO] == 0);

    // click: preloaded -> exactly one I2C transaction (the GO write)
    CHECK(hal_haptic_click_prepare(4) == HAL_OK && regs[DRV_REG_SEQ1] == 4);
    uint32_t t0 = hal_i2c_mock_transaction_count();
    s_go_writes = 0;
    CHECK(hal_haptic_click() == HAL_OK);
    CHECK(hal_i2c_mock_transaction_count() - t0 == 1 && s_go_writes == 1 && regs[DRV_REG_GO] == 1);
    CHECK(hal_haptic_last_click_us() < 1000000u);
    // after another effect overwrote the sequencer, click reloads first (2 transactions)
    CHECK(hal_haptic_play(9) == HAL_OK);
    t0 = hal_i2c_mock_transaction_count();
    CHECK(hal_haptic_click() == HAL_OK && hal_i2c_mock_transaction_count() - t0 == 2);
    CHECK(regs[DRV_REG_SEQ1] == 4);

    // RTP: signed, 0..127
    CHECK(hal_haptic_rtp_begin() == HAL_OK && regs[DRV_REG_MODE] == DRV_MODE_RTP);
    CHECK(hal_haptic_rtp_set(127) == HAL_OK && regs[DRV_REG_RTP] == 0x7F);
    CHECK(hal_haptic_rtp_set(-1) == HAL_OK && regs[DRV_REG_RTP] == 0xFF);
    CHECK(hal_haptic_rtp_end() == HAL_OK && regs[DRV_REG_MODE] == 0 && regs[DRV_REG_RTP] == 0);
    CHECK(hal_haptic_standby(true) == HAL_OK && regs[DRV_REG_MODE] == DRV_MODE_STANDBY);
    CHECK(hal_haptic_standby(false) == HAL_OK && regs[DRV_REG_MODE] == 0);

    // LRA path: library 6, LRA bit set, calibration cleared
    CHECK(hal_haptic_set_actuator(HAL_HAPTIC_LRA) == HAL_OK);
    CHECK(regs[DRV_REG_LIBRARY] == 6 && (regs[DRV_REG_FEEDBACK] & DRV_FB_LRA));
    CHECK(regs[DRV_REG_OD_CLAMP] == 169);
    CHECK(hal_haptic_set_actuator(HAL_HAPTIC_ERM) == HAL_OK && regs[DRV_REG_LIBRARY] == 2);

    // ERM open loop (the build default): bit set, clamp 166, calibration not applicable
    CHECK(hal_haptic_set_erm_loop(HAL_HAPTIC_LOOP_OPEN) == HAL_OK);
    CHECK((regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP) && regs[DRV_REG_OD_CLAMP] == 166);
    CHECK(regs[DRV_REG_RATED] == 141 && regs[DRV_REG_LIBRARY] == 2 && !(regs[DRV_REG_FEEDBACK] & DRV_FB_LRA));
    CHECK(hal_haptic_calibrate(&cal) == HAL_ENOTSUP && regs[DRV_REG_MODE] == 0);
    CHECK(hal_haptic_play(1) == HAL_OK && hal_haptic_rtp_begin() == HAL_OK && hal_haptic_rtp_set(127) == HAL_OK);
    CHECK(hal_haptic_rtp_end() == HAL_OK);
    CHECK(hal_haptic_set_erm_loop(HAL_HAPTIC_LOOP_CLOSED) == HAL_OK);
    CHECK(!(regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP) && regs[DRV_REG_OD_CLAMP] == 182);
    CHECK(hal_haptic_set_erm_loop(HAL_HAPTIC_LOOP_OPEN) == HAL_OK);
    CHECK(hal_haptic_set_actuator(HAL_HAPTIC_LRA) == HAL_OK && !(regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP));
    CHECK(hal_haptic_set_actuator(HAL_HAPTIC_ERM) == HAL_OK && (regs[DRV_REG_CONTROL3] & DRV_CTRL3_ERM_OPEN_LOOP));

    // safe state: RTP 0, GO 0, standby, regardless of init
    CHECK(hal_haptic_rtp_begin() == HAL_OK && hal_haptic_rtp_set(100) == HAL_OK);
    CHECK(hal_haptic_safe_state() == HAL_OK);
    CHECK(regs[DRV_REG_RTP] == 0 && regs[DRV_REG_GO] == 0 && regs[DRV_REG_MODE] == DRV_MODE_STANDBY);

    // safety invariants across everything above: init, calibrations, failures, actuator switches
    CHECK(s_max_rated <= 141);
    CHECK(s_max_od <= 182 && s_max_od_ol == 166);
    CHECK(s_max_od_lra <= 169);
    CHECK(s_viol == 0);

    printf(s_fail ? "test_drv2605l: %d FAILED\n" : "test_drv2605l: ok\n", s_fail);
    return s_fail ? 1 : 0;
}
