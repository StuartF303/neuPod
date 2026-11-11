// SPDX-License-Identifier: MPL-2.0
// Y2 charging-only mode logic: charger detection from a fake power_supply tree, the 3 s unplug debounce, the Power hold
// requesting a reboot exactly once, and a short press doing nothing. Time is injected; nothing is ever spawned here.
#include "unit.h"
#include "y2_charge.h"
#include "y2_fake_fs.h"

static void key(y2_charge_t *c, uint16_t code, int32_t value, uint32_t t) {
    y2_event_t ev = {Y2_EV_KEY, code, value};
    y2_charge_feed(c, &ev, t);
}

TEST(online_reads_usb_and_ac_and_ignores_the_battery) {
    char p[400];
    fs_new();
    ASSERT_EQ(Y2_CHARGE_ONLINE_UNKNOWN, y2_charge_read_online(fs_path("none", p, sizeof p)));
    fs_write("ps/battery/type", "Battery\n");
    fs_write("ps/battery/online", "1\n"); // a battery never counts, even with a stray "online"
    ASSERT_EQ(Y2_CHARGE_ONLINE_UNKNOWN, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/usb/type", "USB\n");
    fs_write("ps/usb/online", "0\n");
    ASSERT_EQ(Y2_CHARGE_OFFLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/ac/type", "Mains\n");
    fs_write("ps/ac/online", "0\n");
    ASSERT_EQ(Y2_CHARGE_OFFLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/usb/online", "1\n");
    ASSERT_EQ(Y2_CHARGE_ONLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/usb/online", "0\n");
    fs_write("ps/ac/online", "1\n");
    ASSERT_EQ(Y2_CHARGE_ONLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_destroy();
}

TEST(online_ignores_missing_or_odd_values) {
    char p[400];
    fs_new();
    fs_write("ps/usb/type", "USB\n"); // type readable, no online file
    ASSERT_EQ(Y2_CHARGE_ONLINE_UNKNOWN, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/usb/online", "2\n"); // not 0 or 1
    ASSERT_EQ(Y2_CHARGE_ONLINE_UNKNOWN, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/usb/online", "yes\n");
    ASSERT_EQ(Y2_CHARGE_ONLINE_UNKNOWN, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/ac/online", "0\n"); // type unreadable: still a supply, still read
    ASSERT_EQ(Y2_CHARGE_OFFLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_write("ps/ac/online", "1\n");
    ASSERT_EQ(Y2_CHARGE_ONLINE, y2_charge_read_online(fs_path("ps", p, sizeof p)));
    fs_destroy();
}

TEST(unplug_is_not_armed_until_online_was_seen) {
    y2_charge_t c;
    y2_charge_init(&c);
    // online=0 from the start (charger detection lag): never powers off, however long
    for (uint32_t t = 0; t <= 60000; t += 250) {
        ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_OFFLINE));
    }
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 61000, Y2_CHARGE_ONLINE)); // detected: armed now
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 61250, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 64249, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_POWEROFF, y2_charge_step(&c, 64250, Y2_CHARGE_OFFLINE));
}

TEST(reboot_beats_poweroff_in_the_same_step) {
    y2_charge_t c;
    y2_charge_init(&c);
    (void)y2_charge_step(&c, 0, Y2_CHARGE_ONLINE);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 1000, Y2_CHARGE_OFFLINE)); // offline since 1000: due at 4000
    key(&c, Y2_KEY_POWER, 1, 2000);                                         // held since 2000: due at 4000
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 3750, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_REBOOT, y2_charge_step(&c, 4000, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_POWEROFF, y2_charge_step(&c, 4250, Y2_CHARGE_OFFLINE)); // the poweroff is still owed
}

TEST(unplug_needs_three_continuous_seconds_and_a_blip_resets_it) {
    y2_unplug_t u;
    y2_unplug_init(&u);
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_ONLINE, 0));
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_OFFLINE, 1000)); // offline from t=1000
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_OFFLINE, 3999));
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_ONLINE, 4000)); // a blip: the timer starts again
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_OFFLINE, 4250));
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_OFFLINE, 7249)); // 2999 ms since 4250
    ASSERT_TRUE(y2_unplug_update(&u, Y2_CHARGE_OFFLINE, 7250));  // exactly 3000 ms
    // an unreadable charger is "not proven offline": it also restarts the timer, and never fires on its own
    y2_unplug_init(&u);
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_ONLINE_UNKNOWN, 0));
    ASSERT_FALSE(y2_unplug_update(&u, Y2_CHARGE_ONLINE_UNKNOWN, 60000));
}

TEST(step_requests_poweroff_once_after_a_clean_three_seconds) {
    y2_charge_t c;
    y2_charge_init(&c);
    uint32_t t = 0;
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_ONLINE));
    for (t = 250; t < 2250; t += 250) {
        ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_OFFLINE)); // offline since 250
    }
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 2250, Y2_CHARGE_ONLINE)); // blip
    for (t = 2500; t < 5500; t += 250) {
        ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_OFFLINE)); // offline since 2500, 3000 ms at 5500
    }
    ASSERT_EQ(Y2_CHARGE_POWEROFF, y2_charge_step(&c, 5500, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 5750, Y2_CHARGE_OFFLINE)); // once
    // the command could not be started: it is tried again after another 3 s offline
    y2_charge_failed(&c, Y2_CHARGE_POWEROFF, 5750);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 8749, Y2_CHARGE_OFFLINE));
    ASSERT_EQ(Y2_CHARGE_POWEROFF, y2_charge_step(&c, 8750, Y2_CHARGE_OFFLINE));
}

TEST(power_hold_requests_reboot_exactly_once) {
    y2_charge_t c;
    y2_charge_init(&c);
    key(&c, Y2_KEY_POWER, 1, 1000);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 1250, Y2_CHARGE_ONLINE));
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 2999, Y2_CHARGE_ONLINE)); // 1999 ms held
    ASSERT_EQ(Y2_CHARGE_REBOOT, y2_charge_step(&c, 3000, Y2_CHARGE_ONLINE)); // 2000 ms, still held
    for (uint32_t t = 3250; t < 8000; t += 250) {
        ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_ONLINE)); // keeps being held: no repeat
    }
    key(&c, Y2_KEY_POWER, 0, 8000);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 8000, Y2_CHARGE_ONLINE)); // release does nothing
    key(&c, Y2_KEY_POWER, 1, 9000); // a second hold also stays quiet: the reboot was already requested
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 12000, Y2_CHARGE_ONLINE));
    // if the reboot command could not be started the next hold may try again
    y2_charge_failed(&c, Y2_CHARGE_REBOOT, 12000);
    key(&c, Y2_KEY_POWER, 0, 12100);
    key(&c, Y2_KEY_POWER, 1, 13000);
    ASSERT_EQ(Y2_CHARGE_REBOOT, y2_charge_step(&c, 15000, Y2_CHARGE_ONLINE));
}

TEST(short_press_and_other_keys_do_nothing) {
    y2_charge_t c;
    y2_charge_init(&c);
    key(&c, Y2_KEY_POWER, 1, 1000);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 1000, Y2_CHARGE_ONLINE));
    key(&c, Y2_KEY_POWER, 0, 1500);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 1500, Y2_CHARGE_ONLINE));
    // later steps stay quiet, long after the 2 s mark
    for (uint32_t t = 1750; t < 10000; t += 250) {
        ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, t, Y2_CHARGE_ONLINE));
    }
    key(&c, Y2_KEY_ENTER, 1, 10000); // other keys are ignored
    key(&c, Y2_KEY_ENTER, 0, 10100);
    key(&c, Y2_KEY_VOLUMEUP, 1, 10200);
    ASSERT_EQ(Y2_CHARGE_NONE, y2_charge_step(&c, 20000, Y2_CHARGE_ONLINE));
}

int main(void) {
    RUN(online_reads_usb_and_ac_and_ignores_the_battery);
    RUN(online_ignores_missing_or_odd_values);
    RUN(unplug_is_not_armed_until_online_was_seen);
    RUN(reboot_beats_poweroff_in_the_same_step);
    RUN(unplug_needs_three_continuous_seconds_and_a_blip_resets_it);
    RUN(step_requests_poweroff_once_after_a_clean_three_seconds);
    RUN(power_hold_requests_reboot_exactly_once);
    RUN(short_press_and_other_keys_do_nothing);
    return RESULT();
}
