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
    y2_hciup_plan_init(&p, false, false, 0, false, false, false, false, 0);
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
    y2_hciup_plan_init(&p, true, false, 0, false, false, false, true, -4);
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
    y2_hciup_plan_init(&p, false, false, 0, false, false, false, true, 6);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C59, c.opcode);
    ASSERT_EQ(6, c.params[0]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READBACK, c.step);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(inq_mode_parse) {
    uint8_t v = 9;
    ASSERT_EQ(Y2_INQ_MODE_UNSET, y2_bt_parse_inq_mode(NULL, &v));
    ASSERT_EQ(Y2_INQ_MODE_UNSET, y2_bt_parse_inq_mode("", &v));
    ASSERT_EQ(9, v);
    const char *bad[] = {"3", "-1", "1x", " 1", "1 ", "01", "a"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ASSERT_EQ(Y2_INQ_MODE_INVALID, y2_bt_parse_inq_mode(bad[i], &v));
    }
    ASSERT_EQ(9, v);
    for (int m = 0; m <= 2; m++) {
        char str[2] = {(char)('0' + m), 0};
        ASSERT_EQ(Y2_INQ_MODE_OK, y2_bt_parse_inq_mode(str, &v));
        ASSERT_EQ(m, v);
    }
}

TEST(inq_mode_only_plan_is_write_then_read) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, true, 0, false, false, false, false, 0); // mode 0 still sends
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_WRITE_INQ_MODE, c.step);
    ASSERT_EQ(0x0C45, c.opcode);
    ASSERT_EQ(1, c.len);
    ASSERT_EQ(0, c.params[0]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(all_switches_plan_order) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, true, 1, false, false, false, true, 6);
    const uint16_t order[] = {0xFC66, 0x0C45, 0x0C58, 0x0C59, 0x0C58};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        ASSERT_TRUE(y2_hciup_next(&p, &c));
        ASSERT_EQ(order[i], c.opcode);
        if (c.opcode == 0x0C45) {
            ASSERT_EQ(1, c.params[0]);
        }
    }
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(stock_init_parse) {
    ASSERT_EQ(Y2_STOCK_INIT_UNSET, y2_bt_parse_stock_init(NULL));
    ASSERT_EQ(Y2_STOCK_INIT_UNSET, y2_bt_parse_stock_init(""));
    ASSERT_EQ(Y2_STOCK_INIT_ON, y2_bt_parse_stock_init("1"));
    const char *bad[] = {"0", "11", "yes", " 1", "1 ", "true"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ASSERT_EQ(Y2_STOCK_INIT_INVALID, y2_bt_parse_stock_init(bad[i]));
    }
}

TEST(stock_init_only_plan) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, false, 0, true, false, false, false, 0);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_EVENT_MASK, c.step);
    ASSERT_EQ(0x0C01, c.opcode);
    ASSERT_EQ(8, c.len);
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(0xFF, c.params[i]);
    }
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C6D, c.opcode);
    ASSERT_EQ(2, c.len);
    ASSERT_EQ(0x01, c.params[0]);
    ASSERT_EQ(0x00, c.params[1]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C18, c.opcode);
    ASSERT_EQ(2, c.len);
    ASSERT_EQ(0x00, c.params[0]);
    ASSERT_EQ(0x38, c.params[1]);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READ_TXPWR, c.step);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(all_switches_with_stock_init_order) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, true, 2, true, false, false, true, 6);
    const uint16_t order[] = {0xFC66, 0x0C45, 0x0C01, 0x0C6D, 0x0C18, 0x0C58, 0x0C59, 0x0C58};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        ASSERT_TRUE(y2_hciup_next(&p, &c));
        ASSERT_EQ(order[i], c.opcode);
    }
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(stock_init_off_adds_nothing) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, true, 2, false, false, false, true, 6);
    const uint16_t order[] = {0xFC66, 0x0C45, 0x0C58, 0x0C59, 0x0C58};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        ASSERT_TRUE(y2_hciup_next(&p, &c));
        ASSERT_EQ(order[i], c.opcode);
    }
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(switch_parse) {
    ASSERT_EQ(Y2_SWITCH_UNSET, y2_bt_parse_switch(NULL));
    ASSERT_EQ(Y2_SWITCH_UNSET, y2_bt_parse_switch(""));
    ASSERT_EQ(Y2_SWITCH_ON, y2_bt_parse_switch("1"));
    const char *bad[] = {"0", "11", "yes", " 1", "1 ", "true", "on"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ASSERT_EQ(Y2_SWITCH_INVALID, y2_bt_parse_switch(bad[i]));
    }
}

TEST(fc54_plan_bytes_and_position) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, false, 0, true, true, false, false, 0);
    const uint16_t order[] = {0xFC66, 0x0C01, 0x0C6D, 0xFC54, 0x0C18, 0x0C58};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        ASSERT_TRUE(y2_hciup_next(&p, &c));
        ASSERT_EQ(order[i], c.opcode);
        if (c.opcode == 0xFC54) {
            ASSERT_EQ(Y2_HCIUP_FC54, c.step);
            ASSERT_EQ(1, c.len);
            ASSERT_EQ(0x01, c.params[0]);
        }
    }
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(fc54_alone_is_one_command_then_the_read) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, false, 0, false, true, false, false, 0);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0xFC54, c.opcode);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_READ_TXPWR, c.step);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(le_scan_params_bytes_and_position) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, false, false, 0, false, false, true, true, 6);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_PARAMS, c.step);
    ASSERT_EQ(0x200B, c.opcode);
    ASSERT_EQ(7, c.len);
    const uint8_t want[7] = {0x01, 0x40, 0x01, 0x40, 0x01, 0x00, 0x00};
    for (int i = 0; i < 7; i++) {
        ASSERT_EQ(want[i], c.params[i]);
    }
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode); // before the power commands
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C59, c.opcode);
    ASSERT_TRUE(y2_hciup_next(&p, &c));
    ASSERT_EQ(0x0C58, c.opcode);
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(everything_on_order) {
    y2_hciup_plan_t p;
    y2_hciup_cmd_t c;
    y2_hciup_plan_init(&p, true, true, 2, true, true, true, true, 6);
    const uint16_t order[] = {0xFC66, 0x0C45, 0x0C01, 0x0C6D, 0xFC54, 0x0C18, 0x200B, 0x0C58, 0x0C59, 0x0C58};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        ASSERT_TRUE(y2_hciup_next(&p, &c));
        ASSERT_EQ(order[i], c.opcode);
    }
    ASSERT_FALSE(y2_hciup_next(&p, &c));
}

TEST(inquiry_sequence_before_without_power) {
    y2_inqseq_t s;
    y2_hciup_cmd_t c;
    y2_inqseq_init(&s, Y2_INQSEQ_BEFORE, false, 0);
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_DISABLE, c.step);
    ASSERT_EQ(0x200C, c.opcode);
    ASSERT_EQ(2, c.len);
    ASSERT_EQ(0x00, c.params[0]);
    ASSERT_EQ(0x00, c.params[1]);
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_ENABLE, c.step);
    ASSERT_EQ(0x200C, c.opcode);
    ASSERT_EQ(2, c.len);
    ASSERT_EQ(0x01, c.params[0]);
    ASSERT_EQ(0x01, c.params[1]);
    ASSERT_FALSE(y2_inqseq_next(&s, &c));
    ASSERT_FALSE(y2_inqseq_next(&s, &c)); // stays done
}

TEST(inquiry_sequence_before_with_power_between) {
    y2_inqseq_t s;
    y2_hciup_cmd_t c;
    y2_inqseq_init(&s, Y2_INQSEQ_BEFORE, true, -4);
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_DISABLE, c.step);
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_WRITE_TXPWR, c.step);
    ASSERT_EQ(0x0C59, c.opcode);
    ASSERT_EQ(1, c.len);
    ASSERT_EQ(0xFC, c.params[0]);
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_ENABLE, c.step);
    ASSERT_FALSE(y2_inqseq_next(&s, &c));
}

TEST(inquiry_sequence_after_is_one_disable) {
    y2_inqseq_t s;
    y2_hciup_cmd_t c;
    y2_inqseq_init(&s, Y2_INQSEQ_AFTER, true, 6); // the power is not rewritten after
    ASSERT_TRUE(y2_inqseq_next(&s, &c));
    ASSERT_EQ(Y2_HCIUP_LE_SCAN_DISABLE, c.step);
    ASSERT_EQ(0x200C, c.opcode);
    ASSERT_EQ(2, c.len);
    ASSERT_EQ(0x00, c.params[0]);
    ASSERT_EQ(0x00, c.params[1]);
    ASSERT_FALSE(y2_inqseq_next(&s, &c));
}

int main(void) {
    RUN(unset_and_empty_are_not_sent);
    RUN(valid_values_include_the_limits_and_signs);
    RUN(invalid_values_are_refused_and_leave_out_alone);
    RUN(fc66_is_on_only_for_exactly_one);
    RUN(default_plan_is_only_the_diagnostic_read);
    RUN(full_plan_is_fc66_read_write_readback);
    RUN(write_only_plan_skips_fc66);
    RUN(inq_mode_parse);
    RUN(inq_mode_only_plan_is_write_then_read);
    RUN(all_switches_plan_order);
    RUN(stock_init_parse);
    RUN(stock_init_only_plan);
    RUN(all_switches_with_stock_init_order);
    RUN(stock_init_off_adds_nothing);
    RUN(switch_parse);
    RUN(fc54_plan_bytes_and_position);
    RUN(fc54_alone_is_one_command_then_the_read);
    RUN(le_scan_params_bytes_and_position);
    RUN(everything_on_order);
    RUN(inquiry_sequence_before_without_power);
    RUN(inquiry_sequence_before_with_power_between);
    RUN(inquiry_sequence_after_is_one_disable);
    return RESULT();
}
