// SPDX-License-Identifier: MPL-2.0
// Y2 system sleep, the parts that need no device (platforms/y2/hal/y2_suspend.c, docs/adr/0021-power-sleep.md): the kill switch, the sysfs writes, the alarm ioctl
// numbers and timestamp, and the wait's verdicts from the two clocks, the input nodes and external power.
#include <string.h>

#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_input.h"
#include "y2_suspend.h"

TEST(the_kill_switch_is_only_a_zero) {
    ASSERT_TRUE(y2_susp_env_enabled(NULL));
    ASSERT_TRUE(y2_susp_env_enabled(""));
    ASSERT_TRUE(y2_susp_env_enabled("1"));
    ASSERT_TRUE(y2_susp_env_enabled("yes"));
    ASSERT_FALSE(y2_susp_env_enabled("0"));
}

TEST(the_state_file_must_offer_mem) {
    ASSERT_TRUE(y2_susp_state_has_mem("mem disk\n")); // what the device says
    ASSERT_TRUE(y2_susp_state_has_mem("freeze standby mem\n"));
    ASSERT_TRUE(y2_susp_state_has_mem("mem"));
    ASSERT_FALSE(y2_susp_state_has_mem("disk\n"));
    ASSERT_FALSE(y2_susp_state_has_mem("memory disk\n")); // a word, not a prefix
    ASSERT_FALSE(y2_susp_state_has_mem(""));
    ASSERT_FALSE(y2_susp_state_has_mem(NULL));
}

static const char *read_back(const char *rel, char *out, size_t cap) {
    char path[400];
    fs_path(rel, path, sizeof path);
    FILE *f = fopen(path, "r");
    out[0] = '\0';
    if (f != NULL) {
        size_t n = fread(out, 1, cap - 1, f);
        out[n] = '\0';
        fclose(f);
    }
    return out;
}

TEST(sysfs_writes_go_to_the_named_file_and_fail_when_it_is_missing) {
    fs_new();
    fs_write("power/state", "mem disk\n");
    fs_write("power/wake_lock", "");
    char dir[300], got[64];
    fs_path("power", dir, sizeof dir);
    ASSERT_TRUE(y2_susp_write(dir, "state", "mem"));
    ASSERT_TRUE(strcmp(read_back("power/state", got, sizeof got), "mem") == 0); // no newline added, the file is replaced
    ASSERT_TRUE(y2_susp_write(dir, "wake_lock", Y2_WAKELOCK_NAME));
    ASSERT_TRUE(strcmp(read_back("power/wake_lock", got, sizeof got), "neupod") == 0);
    ASSERT_FALSE(y2_susp_write(dir, "wake_unlock", "neupod")); // not there (a kernel without wake locks): never created
    ASSERT_FALSE(y2_susp_write("/nonexistent/dir", "state", "on"));
    fs_destroy();
}

TEST(the_alarm_ioctls_match_the_android_alarm_driver_layout) {
    ASSERT_EQ(8, sizeof(y2_alarm_ts_t)); // the 32-bit kernel's struct timespec
    ASSERT_EQ(0x6100u, Y2_ALARM_CLEAR(Y2_ALARM_RTC_WAKEUP));                    // _IO('a', 0)
    ASSERT_EQ(0x40086102u, (unsigned)Y2_ALARM_SET(Y2_ALARM_RTC_WAKEUP));         // _IOW('a', 2, 8 bytes)
    ASSERT_EQ(0x40086104u, (unsigned)Y2_ALARM_GET_TIME(Y2_ALARM_RTC_WAKEUP));    // _IOW('a', 4, 8 bytes)
    ASSERT_EQ(0x6110u, Y2_ALARM_CLEAR(1));                                       // the type is shifted into the command's high nibble
    ASSERT_EQ(0x40086112u, (unsigned)Y2_ALARM_SET(1));
}

TEST(the_alarm_time_is_now_plus_the_limit) {
    y2_alarm_ts_t t = y2_alarm_after((y2_alarm_ts_t){.sec = 1000, .nsec = 250000000}, 24u * 3600u * 1000u);
    ASSERT_EQ(1000 + 86400, t.sec);
    ASSERT_EQ(250000000, t.nsec);
    t = y2_alarm_after((y2_alarm_ts_t){.sec = 5, .nsec = 900000000}, 1200);
    ASSERT_EQ(7, t.sec); // the nanoseconds carry
    ASSERT_EQ(100000000, t.nsec);
}

static void put_event(uint8_t *p, uint16_t type, uint16_t code, int32_t value) {
    memset(p, 0, Y2_EVENT_SIZE);
    p[8] = (uint8_t)type;
    p[9] = (uint8_t)(type >> 8);
    p[10] = (uint8_t)code;
    p[11] = (uint8_t)(code >> 8);
    for (int i = 0; i < 4; i++) {
        p[12 + i] = (uint8_t)((uint32_t)value >> (8 * i));
    }
}

TEST(only_a_key_press_counts_as_input_during_a_sleep) {
    uint8_t buf[Y2_EVENT_SIZE * 4];
    put_event(buf, 1, 116, 0);               // a key release (a controller powering down may send these)
    put_event(buf + 16, 3, 0, 120);          // a position
    put_event(buf + 32, 0, 0, 0);            // sync
    ASSERT_FALSE(y2_susp_has_press(buf, 48));
    put_event(buf + 48, 1, 116, 1);          // Power pressed
    ASSERT_TRUE(y2_susp_has_press(buf, 64));
    ASSERT_FALSE(y2_susp_has_press(buf, 15)); // less than a record
    put_event(buf, 1, 116, 2);               // an auto-repeat is not a first press
    ASSERT_FALSE(y2_susp_has_press(buf, 16));
    // more records than the decoder's batch
    uint8_t big[Y2_EVENT_SIZE * 80];
    for (int i = 0; i < 79; i++) {
        put_event(big + i * 16, 0, 0, 0);
    }
    put_event(big + 79 * 16, 1, 28, 1);
    ASSERT_TRUE(y2_susp_has_press(big, sizeof big));
}

#define T0M 100000   /* CLOCK_MONOTONIC at the request, ms */
#define T0B 5000000  /* CLOCK_BOOTTIME at the request, ms */
#define LIMIT (24u * 3600u * 1000u)
#define T0R 1700000000000LL /* the alarm driver's clock at the request, ms */

static y2_susp_t begun(bool ext) {
    y2_susp_t s;
    y2_susp_begin(&s, LIMIT, T0M, T0B, T0R, ext);
    return s;
}

TEST(waiting_until_something_happens) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 20, T0B + 20, -1, false, false, false);
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
    ASSERT_FALSE(v.slept);
    v = y2_susp_step(&s, T0M + Y2_SUSP_REFUSE_MS - 1, T0B + Y2_SUSP_REFUSE_MS - 1, -1, false, false, false);
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
}

TEST(a_key_wake_after_a_real_sleep) {
    y2_susp_t s = begun(false);
    // 40 minutes asleep: the boot clock ran on, the monotonic one stood still
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 800, T0B + 800 + 40 * 60 * 1000, -1, true, false, false);
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_KEY, v.why);
    ASSERT_TRUE(v.slept);
    ASSERT_EQ(40 * 60 * 1000, v.slept_ms);
}

TEST(the_alarm_is_the_timer_wake) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 800, T0B + 800 + LIMIT - 500, -1, false, false, false); // woken by the alarm a little early
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_TIMER, v.why);
    ASSERT_TRUE(v.slept);
}

TEST(a_wake_well_before_the_limit_is_not_the_timer) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 800, T0B + 800 + 3 * 3600 * 1000, -1, false, false, false); // 3 h asleep, nothing watched woke it
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_OTHER, v.why);
}

TEST(plugging_in_the_charger_wakes_it_with_that_reason) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 800, T0B + 800 + 600 * 1000, -1, false, false, true);
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_EXTERNAL_POWER, v.why);
    s = begun(true); // already on the charger at the request: not a change
    v = y2_susp_step(&s, T0M + 800, T0B + 800 + 600 * 1000, -1, false, false, true);
    ASSERT_EQ(HAL_WAKE_OTHER, v.why);
}

TEST(a_key_before_it_has_gone_down_ends_the_attempt_at_once) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 300, T0B + 300, -1, true, false, false);
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_KEY, v.why);
    ASSERT_FALSE(v.slept); // the key is real: the core lights the screen for it
}

TEST(a_system_that_never_suspends_is_given_up_on) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + Y2_SUSP_REFUSE_MS, T0B + Y2_SUSP_REFUSE_MS, -1, false, false, false);
    ASSERT_EQ(Y2_SUSP_REFUSED, v.state);
    ASSERT_FALSE(v.slept);
}

TEST(a_short_nap_is_not_a_sleep_but_the_difference_adds_up) {
    y2_susp_t s = begun(false);
    // 1.0 s asleep so far: under the gap, still waiting
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 5000, T0B + 5000 + 1000, -1, false, false, false);
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
    // the kernel suspended again after a spurious wake: 3 s in all
    v = y2_susp_step(&s, T0M + 5000, T0B + 5000 + 3000, -1, false, false, false);
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_TRUE(v.slept);
}

TEST(a_clock_that_steps_back_cannot_fake_a_sleep) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 5000, T0B + 4000, -1, false, false, false); // the boot clock reads lower than the monotonic one's advance
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
    ASSERT_FALSE(v.slept);
    ASSERT_EQ(0, v.slept_ms);
}

TEST(the_real_time_clock_decides_the_timer_when_the_boot_clock_did_not_run_on) {
    y2_susp_t s = begun(false);
    // the boot clock shows nothing (it did not advance across the sleep) but the alarm's own clock reached the deadline
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 800, T0B + 800, T0R + (int64_t)LIMIT + 200, false, false, false);
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_TIMER, v.why);
    ASSERT_TRUE(v.slept);
    v = y2_susp_step(&s, T0M + 800, T0B + 800, T0R + 5000, false, false, false); // an unremarkable reading: not due
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
    v = y2_susp_step(&s, T0M + 800, T0B + 800, -1, false, false, false); // unreadable: ignored
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
}

TEST(wheel_activity_counts_as_a_key_only_after_the_system_has_slept) {
    y2_susp_t s = begun(false);
    y2_susp_verdict_t v = y2_susp_step(&s, T0M + 300, T0B + 300, -1, false, true, false); // the panel twitching as it powers down
    ASSERT_EQ(Y2_SUSP_WAIT, v.state);
    v = y2_susp_step(&s, T0M + 800, T0B + 800 + 600 * 1000, -1, false, true, false); // the wheel woke it: not a press, still a key
    ASSERT_EQ(Y2_SUSP_WOKE, v.state);
    ASSERT_EQ(HAL_WAKE_KEY, v.why);
}

int main(void) {
    RUN(the_kill_switch_is_only_a_zero);
    RUN(the_state_file_must_offer_mem);
    RUN(sysfs_writes_go_to_the_named_file_and_fail_when_it_is_missing);
    RUN(the_alarm_ioctls_match_the_android_alarm_driver_layout);
    RUN(the_alarm_time_is_now_plus_the_limit);
    RUN(only_a_key_press_counts_as_input_during_a_sleep);
    RUN(waiting_until_something_happens);
    RUN(a_key_wake_after_a_real_sleep);
    RUN(the_alarm_is_the_timer_wake);
    RUN(a_wake_well_before_the_limit_is_not_the_timer);
    RUN(plugging_in_the_charger_wakes_it_with_that_reason);
    RUN(a_key_before_it_has_gone_down_ends_the_attempt_at_once);
    RUN(a_system_that_never_suspends_is_given_up_on);
    RUN(a_short_nap_is_not_a_sleep_but_the_difference_adds_up);
    RUN(a_clock_that_steps_back_cannot_fake_a_sleep);
    RUN(the_real_time_clock_decides_the_timer_when_the_boot_clock_did_not_run_on);
    RUN(wheel_activity_counts_as_a_key_only_after_the_system_has_slept);
    return RESULT();
}
