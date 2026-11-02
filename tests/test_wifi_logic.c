// SPDX-License-Identifier: MPL-2.0
// Clock maths and the EU DST rule, reconnect backoff, power policy.
#include "sys_clock.h"
#include "unit.h"
#include "wifi_logic.h"

TEST(test_calendar) {
    clock_tm_t t;
    clock_unix_to_tm(0, &t);
    ASSERT_EQ(1970, t.y); ASSERT_EQ(1, t.mo); ASSERT_EQ(1, t.d); ASSERT_EQ(4, t.wday);
    uint32_t u = clock_tm_to_unix(2026, 11, 2, 21, 30, 15);
    clock_unix_to_tm(u, &t);
    ASSERT_EQ(2026, t.y); ASSERT_EQ(11, t.mo); ASSERT_EQ(2, t.d); ASSERT_EQ(21, t.h); ASSERT_EQ(30, t.mi); ASSERT_EQ(15, t.s);
    ASSERT_EQ(1, t.wday); // Monday
    clock_unix_to_tm(clock_tm_to_unix(2024, 2, 29, 23, 59, 59), &t);
    ASSERT_EQ(2024, t.y); ASSERT_EQ(2, t.mo); ASSERT_EQ(29, t.d);
    clock_unix_to_tm(clock_tm_to_unix(2024, 2, 29, 23, 59, 59) + 1, &t);
    ASSERT_EQ(3, t.mo); ASSERT_EQ(1, t.d);
}

TEST(test_eu_dst_edges) {
    // 2026: last Sunday of March is the 29th, of October the 25th (UTC 01:00).
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2026, 3, 29, 0, 59, 59)));
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2026, 3, 29, 1, 0, 0)));
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2026, 10, 25, 0, 59, 59)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2026, 10, 25, 1, 0, 0)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2026, 1, 15, 12, 0, 0)));
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2026, 7, 1, 12, 0, 0)));
    // other years: 2025 (30 Mar / 26 Oct), 2024 (31 Mar / 27 Oct), 2027 (28 Mar / 31 Oct), 2032 (28 Mar / 31 Oct)
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2025, 3, 30, 1, 0, 0)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2025, 3, 30, 0, 59, 59)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2025, 10, 26, 1, 0, 0)));
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2024, 3, 31, 1, 0, 0)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2024, 10, 27, 1, 0, 0)));
    ASSERT_TRUE(clock_eu_dst(clock_tm_to_unix(2027, 10, 31, 0, 59, 59)));
    ASSERT_FALSE(clock_eu_dst(clock_tm_to_unix(2027, 10, 31, 1, 0, 0)));
}

TEST(test_local_time_zones) {
    clock_tm_t t;
    uint32_t summer = clock_tm_to_unix(2026, 7, 1, 12, 0, 0), winter = clock_tm_to_unix(2026, 1, 1, 12, 0, 0);
    clock_local_tm(summer, CLOCK_ZONE_LONDON, &t);
    ASSERT_EQ(13, t.h); ASSERT_TRUE(t.dst); // BST
    clock_local_tm(winter, CLOCK_ZONE_LONDON, &t);
    ASSERT_EQ(12, t.h); ASSERT_FALSE(t.dst); // GMT
    clock_local_tm(summer, CLOCK_ZONE_CET, &t);
    ASSERT_EQ(14, t.h);
    clock_local_tm(winter, CLOCK_ZONE_EET, &t);
    ASSERT_EQ(14, t.h);
    clock_local_tm(summer, CLOCK_ZONE_UTC, &t);
    ASSERT_EQ(12, t.h); // UTC never shifts
    // the local date can differ from the UTC date
    clock_local_tm(clock_tm_to_unix(2026, 6, 30, 23, 30, 0), CLOCK_ZONE_LONDON, &t);
    ASSERT_EQ(7, t.mo); ASSERT_EQ(1, t.d); ASSERT_EQ(0, t.h); ASSERT_EQ(30, t.mi);
}

TEST(test_running_clock) {
    ASSERT_FALSE(clock_synced());
    ASSERT_EQ(0, clock_utc_at(5000));
    clock_set_utc_at(1000000, 5000, true);
    ASSERT_TRUE(clock_ntp_synced());
    ASSERT_EQ(1000000, clock_utc_at(5000));
    ASSERT_EQ(1000010, clock_utc_at(15999)); // monotonic millis advance it
    ASSERT_EQ(1000011, clock_utc_at(16000));
    clock_set_utc_at(2000000, 100, false);
    ASSERT_FALSE(clock_ntp_synced());
    ASSERT_TRUE(clock_synced());
}

TEST(test_backoff) {
    wifi_backoff_t b;
    wifi_backoff_init(&b);
    ASSERT_TRUE(wifi_backoff_due(&b, 0));
    uint32_t t = 1000, expect[] = {2000, 4000, 8000, 16000, 32000, 64000, 120000, 120000};
    for (int i = 0; i < 8; i++) {
        wifi_backoff_fail(&b, t);
        ASSERT_FALSE(wifi_backoff_due(&b, t + expect[i] - 1));
        ASSERT_TRUE(wifi_backoff_due(&b, t + expect[i]));
        t += expect[i];
    }
    ASSERT_EQ(8, b.failures);
    wifi_backoff_success(&b);
    ASSERT_EQ(0, b.failures);
    ASSERT_EQ(0, b.reconnects); // the first join is not a reconnect
    wifi_backoff_fail(&b, t);
    ASSERT_FALSE(wifi_backoff_due(&b, t + 1999)); // back to 2 s
    wifi_backoff_success(&b);
    ASSERT_EQ(1, b.reconnects);
    // wrap-around of the millisecond counter
    wifi_backoff_fail(&b, 0xFFFFFF00u);
    ASSERT_FALSE(wifi_backoff_due(&b, 0xFFFFFF00u + 100));
    ASSERT_TRUE(wifi_backoff_due(&b, 0xFFFFFF00u + 2000));
}

TEST(test_power_policy) {
    wifi_policy_in_t in = {.wifi_enabled = true, .idle_off_min = 5};
    ASSERT_EQ(WIFI_PW_ON, wifi_policy_decide(&in, 1000)); // idle but within 5 minutes
    ASSERT_EQ(WIFI_PW_OFF, wifi_policy_decide(&in, 5 * 60000u)); // idle for 5 minutes: off
    in.want_online = true;
    ASSERT_EQ(WIFI_PW_ON, wifi_policy_decide(&in, 10 * 60000u)); // someone needs it
    in.playing = true; // playback: off by default, whatever is wanted
    ASSERT_EQ(WIFI_PW_OFF, wifi_policy_decide(&in, 0));
    in.during_playback = true;
    ASSERT_EQ(WIFI_PW_ON, wifi_policy_decide(&in, 0));
    in.wifi_enabled = false;
    ASSERT_EQ(WIFI_PW_OFF, wifi_policy_decide(&in, 0));
    wifi_policy_in_t never = {.wifi_enabled = true, .idle_off_min = 0};
    ASSERT_EQ(WIFI_PW_ON, wifi_policy_decide(&never, 99999999u)); // 0 = never switch off when idle
}

int main(void) {
    RUN(test_calendar);
    RUN(test_eu_dst_edges);
    RUN(test_local_time_zones);
    RUN(test_running_clock);
    RUN(test_backoff);
    RUN(test_power_policy);
    return RESULT();
}
