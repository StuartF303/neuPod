// SPDX-License-Identifier: MPL-2.0
// Y2 battery from a fake /sys/class/power_supply (the real MediaTek node layout, measured on the device).
#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_power.h"

static char g_ps[300];

static void tree(void) {
    fs_new();
    fs_path("ps", g_ps, sizeof g_ps);
    // ac / usb / wireless are not batteries; they must be skipped whatever the directory order.
    fs_write("ps/ac/type", "Mains\n");
    fs_write("ps/ac/online", "0\n");
    fs_write("ps/usb/type", "USB\n");
    fs_write("ps/usb/online", "1\n");
    fs_write("ps/wireless/type", "Wireless\n");
}

static void battery(const char *cap, const char *mv_file, const char *mv, const char *status) {
    fs_write("ps/battery/type", "Battery\n");
    fs_write("ps/battery/capacity", cap);
    fs_write(mv_file, mv);
    fs_write("ps/battery/status", status);
    fs_write("ps/battery/present", "1\n");
}

TEST(parses_the_mediatek_node) {
    tree();
    battery("100\n", "ps/battery/batt_vol", "4222\n", "Full\n");
    hal_battery_status_t b;
    y2_power_status_t st;
    ASSERT_TRUE(y2_power_read(g_ps, &b, &st));
    ASSERT_TRUE(b.valid);
    ASSERT_TRUE(b.present);
    ASSERT_EQ(4222, b.mv);
    ASSERT_EQ(100, b.soc_pct);
    ASSERT_EQ(25600, b.soc_x256);
    ASSERT_EQ(Y2_PS_FULL, st);
    ASSERT_EQ(0, b.rate_mpct_per_h); // full: "idle" in the UI
    ASSERT_EQ(0, b.alerts);
    fs_destroy();
}

TEST(charging_and_discharging_set_a_synthetic_rate_the_ui_understands) {
    tree();
    battery("55\n", "ps/battery/batt_vol", "3900\n", "Charging\n");
    hal_battery_status_t b;
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_TRUE(b.rate_mpct_per_h > 2000); // the UI shows "charging" above +2000
    ASSERT_EQ(55, b.soc_pct);
    battery("55\n", "ps/battery/batt_vol", "3900\n", "Discharging\n");
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_TRUE(b.rate_mpct_per_h < -2000);
    battery("55\n", "ps/battery/batt_vol", "3900\n", "Not charging\n");
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_EQ(0, b.rate_mpct_per_h);
    fs_destroy();
}

TEST(standard_microvolt_voltage_now_is_accepted) {
    tree();
    battery("80\n", "ps/battery/voltage_now", "3987000\n", "Discharging\n");
    hal_battery_status_t b;
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_EQ(3987, b.mv);
    fs_destroy();
}

TEST(capacity_is_clamped) {
    tree();
    battery("140\n", "ps/battery/batt_vol", "4000\n", "Full\n");
    hal_battery_status_t b;
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_EQ(100, b.soc_pct);
    battery("-5\n", "ps/battery/batt_vol", "4000\n", "Full\n");
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_EQ(0, b.soc_pct);
    fs_destroy();
}

TEST(no_battery_or_unreadable_fields_fail_cleanly) {
    tree();
    hal_battery_status_t b;
    b.valid = true;
    ASSERT_FALSE(y2_power_read(g_ps, &b, NULL)); // no Battery-type supply
    ASSERT_FALSE(b.valid);
    fs_write("ps/battery/type", "Battery\n");
    ASSERT_FALSE(y2_power_read(g_ps, &b, NULL)); // no capacity
    fs_write("ps/battery/capacity", "50\n");
    ASSERT_FALSE(y2_power_read(g_ps, &b, NULL)); // no voltage
    fs_write("ps/battery/capacity", "garbage\n");
    fs_write("ps/battery/batt_vol", "4000\n");
    ASSERT_FALSE(y2_power_read(g_ps, &b, NULL));
    ASSERT_FALSE(y2_power_read("/nonexistent/ps", &b, NULL));
    fs_destroy();
}

TEST(present_flag_and_floor) {
    tree();
    battery("50\n", "ps/battery/batt_vol", "4000\n", "Discharging\n");
    fs_write("ps/battery/present", "0\n");
    hal_battery_status_t b;
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_FALSE(b.present);
    fs_write("ps/battery/present", "1\n");
    fs_write("ps/battery/batt_vol", "1200\n"); // below HAL_BATTERY_PRESENT_MV: no cell
    ASSERT_TRUE(y2_power_read(g_ps, &b, NULL));
    ASSERT_FALSE(b.present);
    fs_destroy();
}

TEST(status_text_and_voltage_units) {
    ASSERT_EQ(Y2_PS_CHARGING, y2_power_parse_status("Charging"));
    ASSERT_EQ(Y2_PS_DISCHARGING, y2_power_parse_status("Discharging"));
    ASSERT_EQ(Y2_PS_FULL, y2_power_parse_status("Full"));
    ASSERT_EQ(Y2_PS_NOT_CHARGING, y2_power_parse_status("Not charging"));
    ASSERT_EQ(Y2_PS_UNKNOWN, y2_power_parse_status("Unknown"));
    ASSERT_EQ(Y2_PS_UNKNOWN, y2_power_parse_status(NULL));
    ASSERT_EQ(0, y2_power_to_mv(-1));
    ASSERT_EQ(4222, y2_power_to_mv(4222));
    ASSERT_EQ(4222, y2_power_to_mv(4221600));
}

int main(void) {
    RUN(parses_the_mediatek_node);
    RUN(charging_and_discharging_set_a_synthetic_rate_the_ui_understands);
    RUN(standard_microvolt_voltage_now_is_accepted);
    RUN(capacity_is_clamped);
    RUN(no_battery_or_unreadable_fields_fail_cleanly);
    RUN(present_flag_and_floor);
    RUN(status_text_and_voltage_units);
    return RESULT();
}
