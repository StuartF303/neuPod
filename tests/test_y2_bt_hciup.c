// SPDX-License-Identifier: MPL-2.0
// The Y2 bench switches for stock-parity controller set-up: NEUPOD_BT_INQ_TXPWR parsing and the order of the HCI-up commands.
#include "unit.h"
#include "y2_bt_hciup.h"

TEST(unset_and_empty_are_not_sent) {
    int8_t v = 55;
    ASSERT_EQ(Y2_INQ_TXPWR_UNSET, y2_bt_parse_inq_txpwr(NULL, &v));
    ASSERT_EQ(Y2_INQ_TXPWR_UNSET, y2_bt_parse_inq_txpwr("", &v));
    ASSERT_EQ(55, v);
}

TEST(valid_values_include_the_limits_and_signs) {
    int8_t v = 0;
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("6", &v));
    ASSERT_EQ(6, v);
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("+6", &v));
    ASSERT_EQ(6, v);
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("-70", &v));
    ASSERT_EQ(-70, v);
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("20", &v));
    ASSERT_EQ(20, v);
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("0", &v));
    ASSERT_EQ(0, v);
    ASSERT_EQ(Y2_INQ_TXPWR_OK, y2_bt_parse_inq_txpwr("007", &v));
    ASSERT_EQ(7, v);
}

TEST(invalid_values_are_refused_and_leave_out_alone) {
    int8_t v = 99;
    const char *bad[] = {"21", "-71", "abc", "6dBm", " 6", "6 ", "-", "+", "--6", "1.5", "99999999999999", "0x10"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ASSERT_EQ(Y2_INQ_TXPWR_INVALID, y2_bt_parse_inq_txpwr(bad[i], &v));
    }
    ASSERT_EQ(99, v);
}

TEST(fc66_is_on_only_for_exactly_one) {
    ASSERT_TRUE(y2_bt_parse_fc66("1"));
    ASSERT_FALSE(y2_bt_parse_fc66(NULL));
    ASSERT_FALSE(y2_bt_parse_fc66(""));
    ASSERT_FALSE(y2_bt_parse_fc66("0"));
    ASSERT_FALSE(y2_bt_parse_fc66("11"));
    ASSERT_FALSE(y2_bt_parse_fc66("yes"));
}

TEST(default_plan_is_only_the_diagnostic_read) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, false, 0);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_EQ(Y2_HCIUP_READ_TXPWR, c.step);
    ASSERT_EQ(0, c.len);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
    ASSERT_FALSE(y2_hciup_next(&p, &c)); // stays done
}

TEST(full_plan_is_fc66_read_write_readback) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, true, -4);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0xFC66, c.opcode);
    ASSERT_EQ(4, c.len);
    ASSERT_EQ(0x00, c.params[0]);
    ASSERT_EQ(0x10, c.params[1]);
    ASSERT_EQ(0x40, c.params[2]);
    ASSERT_EQ(0x9C, c.params[3]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READ_TXPWR, c.step);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C59, c.opcode);
    ASSERT_EQ(1, c.len);
    ASSERT_EQ(0xFC, c.params[0]); // -4 as a signed byte
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READBACK, c.step);
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(write_only_plan_skips_fc66) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, true, 6);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C59, c.opcode);
    ASSERT_EQ(6, c.params[0]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READBACK, c.step);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

int main(void) {
    RUN(unset_and_empty_are_not_sent);
    RUN(valid_values_include_the_limits_and_signs);
    RUN(invalid_values_are_refused_and_leave_out_alone);
    RUN(fc66_is_on_only_for_exactly_one);
    RUN(default_plan_is_only_the_diagnostic_read);
    RUN(full_plan_is_fc66_read_write_readback);
    RUN(write_only_plan_skips_fc66);
    return RESULT();
}
