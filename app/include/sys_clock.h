// SPDX-License-Identifier: MPL-2.0
/**
 * @file sys_clock.h
 * @brief Wall clock: UTC from SNTP (or set by hand) on top of the monotonic hal_millis(), plus a simple EU daylight-saving rule.
 *
 * hal_millis() stays the only monotonic time base (audio, timeouts); this clock is an offset from it. No tz database: a zone is
 * a base UTC offset in whole hours plus "follows the EU rule" (DST from 01:00 UTC on the last Sunday of March to 01:00 UTC on
 * the last Sunday of October). Presets: UTC, London (UTC+0, EU rule: GMT/BST), Central Europe (+1), Eastern Europe (+2).
 */
#ifndef NEUPOD_SYS_CLOCK_H
#define NEUPOD_SYS_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t y, mo, d, h, mi, s;
    int32_t wday; /* 0 = Sunday */
    bool dst;
} clock_tm_t;

typedef enum { CLOCK_ZONE_UTC, CLOCK_ZONE_LONDON, CLOCK_ZONE_CET, CLOCK_ZONE_EET, CLOCK_ZONE_COUNT } clock_zone_t;
extern const char *const k_clock_zone_names[CLOCK_ZONE_COUNT];

/* Pure calendar maths (host-tested). */
void clock_unix_to_tm(uint32_t unix_s, clock_tm_t *out);   /* UTC fields, wday; dst false */
uint32_t clock_tm_to_unix(int32_t y, int32_t mo, int32_t d, int32_t h, int32_t mi, int32_t s);
bool clock_eu_dst(uint32_t utc);                            /* the EU rule at that UTC instant */
void clock_local_tm(uint32_t utc, clock_zone_t zone, clock_tm_t *out);

/* The running clock (UI/service tasks; one writer at a time, 32-bit reads are atomic on both targets). */
void clock_set_utc(uint32_t unix_s, bool from_ntp);
void clock_set_utc_at(uint32_t unix_s, uint32_t millis_now, bool from_ntp); /* tests */
bool clock_synced(void);          /* the time is known (NTP or set by hand) */
bool clock_ntp_synced(void);
uint32_t clock_utc_now(void);     /* 0 when unknown */
uint32_t clock_utc_at(uint32_t millis_now);
void clock_set_zone(clock_zone_t z);
clock_zone_t clock_zone(void);
bool clock_local_now(clock_tm_t *out); /* false when unknown */

#ifdef __cplusplus
}
#endif

#endif
