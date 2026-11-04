// SPDX-License-Identifier: MPL-2.0
// Click mode setting, click rate gate and the stored calibration record (haptic_logic.h).
#include <string.h>

#include "haptic_logic.h"
#include "unit.h"

TEST(click_mode_values) {
    click_mode_t m = CLICK_PIEZO;
    ASSERT_TRUE(click_mode_from_stored(0, &m) && m == CLICK_OFF);   // the old "off"
    ASSERT_TRUE(click_mode_from_stored(1, &m) && m == CLICK_HAPTIC); // the old "on"
    ASSERT_TRUE(click_mode_from_stored(2, &m) && m == CLICK_PIEZO);
    ASSERT_TRUE(click_mode_from_stored(3, &m) && m == CLICK_BOTH);
    m = CLICK_HAPTIC;
    ASSERT_FALSE(click_mode_from_stored(4, &m));
    ASSERT_FALSE(click_mode_from_stored(255, &m));
    ASSERT_EQ(CLICK_HAPTIC, m); // untouched by a bad value

    ASSERT_EQ(CLICK_HAPTIC, click_mode_next(CLICK_PIEZO));
    ASSERT_EQ(CLICK_OFF, click_mode_next(CLICK_BOTH));
#if NEUPOD_PIEZO_FITTED
    // Piezo -> Haptic -> Both -> Off -> Piezo
    ASSERT_EQ(CLICK_BOTH, click_mode_next(CLICK_HAPTIC));
    ASSERT_EQ(CLICK_PIEZO, click_mode_next(CLICK_OFF));
#else
    // no piezo fitted: Haptic <-> Off only (Off's next lands on Haptic)
    ASSERT_EQ(CLICK_OFF, click_mode_next(CLICK_HAPTIC));
    ASSERT_EQ(CLICK_HAPTIC, click_mode_next(CLICK_OFF));
#endif

    ASSERT_TRUE(click_mode_uses_haptic(CLICK_HAPTIC) && click_mode_uses_haptic(CLICK_BOTH));
    ASSERT_FALSE(click_mode_uses_haptic(CLICK_PIEZO) || click_mode_uses_haptic(CLICK_OFF));
    ASSERT_TRUE(click_mode_uses_piezo(CLICK_PIEZO) && click_mode_uses_piezo(CLICK_BOTH));
    ASSERT_FALSE(click_mode_uses_piezo(CLICK_HAPTIC) || click_mode_uses_piezo(CLICK_OFF));
    ASSERT_TRUE(strcmp(click_mode_name(CLICK_OFF), "Off") == 0 && strcmp(click_mode_name(CLICK_BOTH), "Both") == 0);
}

TEST(rate_gate) {
    haptic_gate_t g = {0};
    ASSERT_TRUE(haptic_gate_pass(&g, 0, 35));    // first request always passes, even at t = 0
    ASSERT_FALSE(haptic_gate_pass(&g, 10, 35));  // a fast wheel spin: dropped, not queued
    ASSERT_FALSE(haptic_gate_pass(&g, 34, 35));
    ASSERT_TRUE(haptic_gate_pass(&g, 35, 35));
    ASSERT_FALSE(haptic_gate_pass(&g, 36, 35));  // measured from the last ACCEPTED click, not the dropped ones
    unsigned passed = 0;
    for (uint32_t t = 100; t < 1100; t += 5) { // 200 steps in one second, one every 5 ms
        passed += haptic_gate_pass(&g, t, 35);
    }
    ASSERT_TRUE(passed >= 25 && passed <= 29);   // about one in seven
    haptic_gate_t w = {.last_ms = 0xFFFFFFF0u, .have = true};
    ASSERT_FALSE(haptic_gate_pass(&w, 5, 35));   // tick counter wrap
    ASSERT_TRUE(haptic_gate_pass(&w, 0x30, 35));
}

TEST(gate_split_and_calibration_drop) {
    haptic_gate_t g = {0};
    ASSERT_TRUE(haptic_gate_ready(&g, 100, 35));
    ASSERT_TRUE(haptic_gate_ready(&g, 101, 35)); // checking does not consume the gap (a click that lost the bus)
    haptic_gate_mark(&g, 101);
    ASSERT_FALSE(haptic_gate_ready(&g, 120, 35));
    ASSERT_TRUE(haptic_gate_ready(&g, 136, 35));
    ASSERT_TRUE(haptic_click_admissible(false));
    ASSERT_FALSE(haptic_click_admissible(true)); // no clicks are queued while calibrating
}

TEST(cal_record) {
    const hal_haptic_lra_params_t lra = HAL_HAPTIC_LRA_LD0832AA;
    const hal_haptic_cal_t cal = {.valid = true, .a_cal_comp = 0x21, .a_cal_bemf = 0x7B, .bemf_gain = 2};
    uint8_t rec[HAPTIC_CAL_RECORD_LEN];
    haptic_cal_encode(HAL_HAPTIC_LRA, &lra, &cal, rec);

    hal_haptic_cal_t got = {0};
    ASSERT_TRUE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &lra, &got));
    ASSERT_TRUE(got.valid && !got.diag_failed);
    ASSERT_EQ(0x21, got.a_cal_comp);
    ASSERT_EQ(0x7B, got.a_cal_bemf);
    ASSERT_EQ(2, got.bemf_gain);

    // an LRA calibration is not used for an ERM, and is dropped when the LRA parameters change
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_ERM, &lra, &got));
    hal_haptic_lra_params_t other = lra;
    other.freq_hz = 170;
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &other, &got));
    other = lra;
    other.rated_mv_rms = 1800;
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &other, &got));
    other = lra;
    other.peak_mv = 2500; // OD_CLAMP is an input of the calibration
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &other, &got));

    // an ERM record matches an ERM whatever the LRA parameters are
    haptic_cal_encode(HAL_HAPTIC_ERM, &lra, &cal, rec);
    ASSERT_TRUE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_ERM, &other, &got));
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &lra, &got));

    // damaged records, and the old 9-byte version 1 record (no peak) is not accepted
    haptic_cal_encode(HAL_HAPTIC_LRA, &lra, &cal, rec);
    {
        const uint8_t v1[9] = {1, 1, 0x21, 0x7B, 2, 0xD0, 0x07, 0xEB, 0x00};
        ASSERT_FALSE(haptic_cal_decode(v1, sizeof v1, HAL_HAPTIC_LRA, &lra, &got));
        uint8_t v1_padded[HAPTIC_CAL_RECORD_LEN] = {1, 1, 0x21, 0x7B, 2, 0xD0, 0x07};
        ASSERT_FALSE(haptic_cal_decode(v1_padded, sizeof v1_padded, HAL_HAPTIC_LRA, &lra, &got));
    }
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec - 1, HAL_HAPTIC_LRA, &lra, &got));
    ASSERT_FALSE(haptic_cal_decode(NULL, 0, HAL_HAPTIC_LRA, &lra, &got));
    rec[0] = 9; // unknown version
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &lra, &got));
    haptic_cal_encode(HAL_HAPTIC_LRA, &lra, &cal, rec);
    rec[4] = 4; // BEMF gain is two bits
    ASSERT_FALSE(haptic_cal_decode(rec, sizeof rec, HAL_HAPTIC_LRA, &lra, &got));
}

int main(void) {
    RUN(click_mode_values);
    RUN(rate_gate);
    RUN(gate_split_and_calibration_drop);
    RUN(cal_record);
    RESULT();
}
