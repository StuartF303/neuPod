// SPDX-License-Identifier: MPL-2.0
#include "power_policy.h"

static const struct {
    uint32_t sec;
    const char *name;
} k_sleep[POWER_SLEEP_STEPS] = {{0, "Never"}, {600, "10 min"}, {1800, "30 min"}, {3600, "60 min"}};

uint32_t power_sleep_seconds(uint8_t idx) { return idx < POWER_SLEEP_STEPS ? k_sleep[idx].sec : 0u; }
const char *power_sleep_name(uint8_t idx) { return idx < POWER_SLEEP_STEPS ? k_sleep[idx].name : "-"; }

/* a - b as a signed difference: right across a wrap of the 32-bit counter */
static int32_t diff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

void power_policy_init(power_policy_t *p, uint32_t now_ms) {
    p->ref_ms = now_ms;
    p->crit_since_ms = now_ms;
    p->crit = false;
}

void power_policy_restart(power_policy_t *p, uint32_t now_ms) { p->ref_ms = now_ms; }

static bool battery_empty(const power_inputs_t *in) {
    if (in->external_power || !in->battery_valid) {
        return false;
    }
    return in->battery_pct <= POWER_CRIT_PCT || (POWER_CRIT_MV != 0u && in->battery_mv != 0u && in->battery_mv < POWER_CRIT_MV);
}

power_action_t power_policy_tick(power_policy_t *p, const power_inputs_t *in) {
    const uint32_t now = in->now_ms;

    /* The battery rule: a streak that any good or missing reading breaks. */
    if (!battery_empty(in)) {
        p->crit = false;
    } else if (!p->crit || diff(now, p->crit_since_ms) < -(int32_t)POWER_FUTURE_MS) { /* a clock that went back starts the streak again */
        p->crit = true;
        p->crit_since_ms = now;
    } else if (diff(now, p->crit_since_ms) >= (int32_t)POWER_CRIT_HOLD_MS) {
        return POWER_ACT_POWEROFF_CRITICAL;
    }

    /* The idle clock. Playing, busy or on external power the player is not idle: the count restarts from now, so it begins when that ends. */
    if (in->audio_playing || in->busy || in->external_power) {
        p->ref_ms = now;
    } else if (diff(in->last_activity_ms, p->ref_ms) > 0 && diff(in->last_activity_ms, now) <= (int32_t)POWER_FUTURE_MS) {
        p->ref_ms = in->last_activity_ms; /* input since: count from it (it may be a little ahead of now; one from a clock that has since jumped back is ignored) */
    }
    int32_t idle = diff(now, p->ref_ms);
    if (idle < -(int32_t)POWER_FUTURE_MS) {
        p->ref_ms = now; /* the clock jumped back past anything a stamp may be ahead by */
        idle = 0;
    }
    if (in->sleep_after_s != 0u && idle >= 0 && (uint32_t)idle >= in->sleep_after_s * 1000u) {
        return POWER_ACT_SLEEP;
    }
    return POWER_ACT_NONE;
}
