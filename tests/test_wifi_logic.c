// SPDX-License-Identifier: MPL-2.0
// Clock maths and the EU DST rule, reconnect backoff, power policy.
#include <string.h>

#include "hal_host.h"
#include "hal_radio.h"
#include "hal_system.h"
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

TEST(test_clock_wrap_and_sanity) {
    // The millisecond counter wraps at 49.7 days: with the hourly re-anchor the time keeps advancing across the wrap.
    clock_set_utc_at(2000000000u, 0xFFFF0000u, true);
    uint32_t ms = 0xFFFF0000u;
    uint32_t utc = 2000000000u;
    for (int hour = 0; hour < 3; hour++) {
        ms += 3600u * 1000u; // wraps through zero during the loop
        utc += 3600u;
        clock_tick_at(ms);
        ASSERT_EQ(utc, clock_utc_at(ms));
    }
    ASSERT_EQ(utc + 30u, clock_utc_at(ms + 30000u));
    ASSERT_FALSE(clock_ntp_plausible(1000));
    ASSERT_FALSE(clock_ntp_plausible(1735689599u));
    ASSERT_TRUE(clock_ntp_plausible(1735689600u));
    ASSERT_TRUE(clock_ntp_plausible(2100000000u));
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

TEST(test_backoff_late_in_uptime) {
    // After 24.8 days (0x90000000 ms) a fresh state must still be due, and a failure arms a normal 2 s wait.
    wifi_backoff_t b;
    wifi_backoff_init(&b);
    ASSERT_TRUE(wifi_backoff_due(&b, 0x90000000u));
    wifi_backoff_fail(&b, 0x90000000u);
    ASSERT_FALSE(wifi_backoff_due(&b, 0x90000000u + 1999u));
    ASSERT_TRUE(wifi_backoff_due(&b, 0x90000000u + 2000u));
    wifi_backoff_success(&b);
    ASSERT_TRUE(wifi_backoff_due(&b, 0xF0000000u)); // success clears the deadline
}

TEST(test_link_loss_is_reported) {
    ASSERT_EQ(HAL_OK, hal_wifi_start());
    ASSERT_EQ(HAL_OK, hal_wifi_join("HomeNet", "secret"));
    for (int i = 0; i < 60 && hal_wifi_state() != HAL_WIFI_UP; i++) {
        hal_wifi_poll();
        hal_delay_ms(20);
    }
    ASSERT_EQ(HAL_WIFI_UP, hal_wifi_state());
    hal_host_wifi_drop_link();
    ASSERT_EQ(HAL_WIFI_IDLE, hal_wifi_state()); // the service sees the edge UP -> IDLE: icon, log, backoff, auto-reconnect
    hal_wifi_stop();
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

TEST(test_scan_security_bits) {
    // cyw43_ll.c: 4 = RSN element, 2 = WPA element, 1 = privacy capability (set by every encrypted network).
    ASSERT_EQ(HAL_WIFI_SEC_OPEN, wifi_sec_from_scan_bits(0));
    ASSERT_EQ(HAL_WIFI_SEC_WPA2, wifi_sec_from_scan_bits(4 | 1)); // a plain WPA2 home router, as the driver reports it
    ASSERT_EQ(HAL_WIFI_SEC_WPA2, wifi_sec_from_scan_bits(4 | 2 | 1)); // WPA + WPA2 mixed
    ASSERT_EQ(HAL_WIFI_SEC_OTHER, wifi_sec_from_scan_bits(2 | 1)); // WPA1 only
    ASSERT_EQ(HAL_WIFI_SEC_OTHER, wifi_sec_from_scan_bits(1)); // WEP
}

TEST(test_log_text) {
    char o[16];
    hal_log_text(o, sizeof o, "Flip 6");
    ASSERT_EQ(0, strcmp(o, "Flip 6"));
    hal_log_text(o, sizeof o, "a\x1b[2Jb\nc\x7f\xc3\xa9");
    ASSERT_EQ(0, strcmp(o, "a?[2Jb?c???"));
    hal_log_text(o, 4, "abcdefgh"); // truncates, always terminated
    ASSERT_EQ(0, strcmp(o, "abc"));
}

TEST(test_abandoned_scan_does_not_wedge) {
    ASSERT_EQ(HAL_OK, hal_wifi_start());
    ASSERT_EQ(HAL_OK, hal_wifi_scan_start());
    ASSERT_EQ(HAL_EBUSY, hal_wifi_scan_start()); // really running
    // Nobody collects the results (the Networks screen was closed). The scan still ends by itself.
    hal_delay_ms(3000);
    ASSERT_EQ(HAL_OK, hal_wifi_scan_start());
    hal_wifi_stop();
}

int main(void) {
    RUN(test_scan_security_bits);
    RUN(test_log_text);
    RUN(test_abandoned_scan_does_not_wedge);
    RUN(test_calendar);
    RUN(test_eu_dst_edges);
    RUN(test_local_time_zones);
    RUN(test_running_clock);
    RUN(test_clock_wrap_and_sanity);
    RUN(test_backoff);
    RUN(test_backoff_late_in_uptime);
    RUN(test_link_loss_is_reported);
    RUN(test_power_policy);
    return RESULT();
}
