// SPDX-License-Identifier: MPL-2.0
#include "hal_system.h"
#include "sys_clock.h"

const char *const k_clock_zone_names[CLOCK_ZONE_COUNT] = {"UTC", "London", "Central Europe", "Eastern Europe"};

/* Days from 1970-01-01 to y-mo-d (Howard Hinnant's civil algorithm). */
static int32_t days_from_civil(int32_t y, int32_t m, int32_t d) {
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint32_t clock_tm_to_unix(int32_t y, int32_t mo, int32_t d, int32_t h, int32_t mi, int32_t s) {
    return (uint32_t)days_from_civil(y, mo, d) * 86400u + (uint32_t)h * 3600u + (uint32_t)mi * 60u + (uint32_t)s;
}

void clock_unix_to_tm(uint32_t u, clock_tm_t *o) {
    int32_t days = (int32_t)(u / 86400u);
    uint32_t rem = u % 86400u;
    o->h = (int32_t)(rem / 3600u);
    o->mi = (int32_t)(rem / 60u % 60u);
    o->s = (int32_t)(rem % 60u);
    o->wday = (days + 4) % 7; /* 1970-01-01 was a Thursday */
    int32_t z = days + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    o->d = doy - (153 * mp + 2) / 5 + 1;
    o->mo = mp < 10 ? mp + 3 : mp - 9;
    o->y = y + (o->mo <= 2);
    o->dst = false;
}

/* The last Sunday of @p month in @p year, as a UTC instant at 01:00. */
static uint32_t last_sunday_0100(int32_t year, int32_t month) {
    int32_t dim = month == 3 ? 31 : 31; /* March and October both have 31 days */
    uint32_t last = clock_tm_to_unix(year, month, dim, 0, 0, 0);
    clock_tm_t t;
    clock_unix_to_tm(last, &t);
    return last - (uint32_t)t.wday * 86400u + 3600u;
}

bool clock_eu_dst(uint32_t utc) {
    clock_tm_t t;
    clock_unix_to_tm(utc, &t);
    return utc >= last_sunday_0100(t.y, 3) && utc < last_sunday_0100(t.y, 10);
}

void clock_local_tm(uint32_t utc, clock_zone_t zone, clock_tm_t *out) {
    int32_t base = zone == CLOCK_ZONE_CET ? 1 : zone == CLOCK_ZONE_EET ? 2 : 0;
    bool dst = zone != CLOCK_ZONE_UTC && clock_eu_dst(utc);
    clock_unix_to_tm(utc + (uint32_t)((base + (dst ? 1 : 0)) * 3600), out);
    out->dst = dst;
}

/* ---- the running clock ----
 * UTC at an anchor plus hal_millis at that anchor must be read together. Two slots and an index: a writer fills the INACTIVE slot and
 * then flips the index, so a reader never waits for a writer (a writer preempted mid-update by a higher-priority reader must not
 * make the reader spin: that is a livelock on a single-core RTOS). The reader re-checks a flip counter and retries a bounded number
 * of times (it only ever sees two flips during one copy if it was itself preempted for two writes). Writers take a try-lock and give
 * up if another writer is inside (clock_set_utc returns false; the caller may retry). clock_tick() re-anchors hourly so the 32-bit
 * millisecond counter never wraps (49.7 days) relative to the anchor. */
#include <stdatomic.h>

typedef struct { uint32_t utc, ms; bool known, ntp; } snap_t;
static snap_t g_slot[2];
static atomic_uint g_idx;   /* the active slot */
static atomic_uint g_flips; /* total flips */
static atomic_flag g_writer = ATOMIC_FLAG_INIT;
static clock_zone_t g_zone = CLOCK_ZONE_LONDON;

static bool write_begin(void) { return !atomic_flag_test_and_set_explicit(&g_writer, memory_order_acquire); }
static void write_end(void) { atomic_flag_clear_explicit(&g_writer, memory_order_release); }

static snap_t read_snap(void) {
    snap_t r = {0};
    for (int tries = 0; tries < 4; tries++) {
        unsigned f0 = atomic_load_explicit(&g_flips, memory_order_acquire);
        unsigned i = atomic_load_explicit(&g_idx, memory_order_acquire);
        r = g_slot[i];
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&g_flips, memory_order_relaxed) - f0 < 2u) {
            break; /* at most one flip meanwhile: the slot we copied was not being rewritten */
        }
    }
    return r;
}

static void publish(snap_t n) { /* caller holds the writer flag */
    unsigned i = 1u - atomic_load_explicit(&g_idx, memory_order_relaxed);
    g_slot[i] = n;
    atomic_store_explicit(&g_idx, i, memory_order_release);
    atomic_fetch_add_explicit(&g_flips, 1u, memory_order_release);
}

bool clock_set_utc_at(uint32_t unix_s, uint32_t millis_now, bool from_ntp) {
    if (!write_begin()) {
        return false;
    }
    publish((snap_t){.utc = unix_s, .ms = millis_now, .known = true, .ntp = from_ntp});
    write_end();
    return true;
}
bool clock_set_utc(uint32_t unix_s, bool from_ntp) { return clock_set_utc_at(unix_s, hal_millis(), from_ntp); }
bool clock_synced(void) { return read_snap().known; }
bool clock_ntp_synced(void) { snap_t s = read_snap(); return s.known && s.ntp; }
uint32_t clock_utc_at(uint32_t millis_now) {
    snap_t s = read_snap();
    return s.known ? s.utc + (millis_now - s.ms) / 1000u : 0;
}
uint32_t clock_utc_now(void) { return clock_utc_at(hal_millis()); }

void clock_tick_at(uint32_t millis_now) {
    snap_t s = read_snap();
    uint32_t elapsed_s = (millis_now - s.ms) / 1000u;
    if (s.known && elapsed_s >= 3600u && write_begin()) { /* move the anchor forward by whole seconds: nothing is lost */
        s.utc += elapsed_s;
        s.ms += elapsed_s * 1000u;
        publish(s);
        write_end();
    }
}
void clock_tick(void) { clock_tick_at(hal_millis()); }

void clock_set_zone(clock_zone_t z) { g_zone = z < CLOCK_ZONE_COUNT ? z : CLOCK_ZONE_UTC; }
clock_zone_t clock_zone(void) { return g_zone; }
bool clock_local_now(clock_tm_t *out) {
    uint32_t u = clock_utc_now();
    if (!clock_synced() || u == 0) {
        return false;
    }
    clock_local_tm(u, g_zone, out);
    return true;
}

bool clock_ntp_plausible(uint32_t unix_s) { return unix_s >= CLOCK_MIN_PLAUSIBLE_UTC; } /* 32-bit Unix time runs to 2106; lwIP's NTP era arithmetic is modulo 2^32 and stays right until then */
