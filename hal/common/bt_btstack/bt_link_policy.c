// SPDX-License-Identifier: MPL-2.0
#include "bt_link_policy.h"

uint32_t bt_link_owed_clamp(uint32_t owed, uint32_t rate, uint32_t *dropped) {
    const uint32_t cap = rate / 1000u * BT_LINK_CATCHUP_MS + (rate % 1000u) * BT_LINK_CATCHUP_MS / 1000u;
    if (owed > cap) {
        if (dropped != 0) {
            *dropped = owed - cap;
        }
        return cap;
    }
    if (dropped != 0) {
        *dropped = 0;
    }
    return owed;
}

bool bt_link_pressured(uint32_t wait_ms, uint32_t owed_ms) { return wait_ms >= BT_LINK_PRESSURE_WAIT_MS || owed_ms >= BT_LINK_PRESSURE_OWED_MS; }

static uint8_t clamp_bp(uint8_t ceiling, uint8_t min_bp, uint8_t want) {
    uint8_t v = want < ceiling ? want : ceiling;
    return v < min_bp ? min_bp : v;
}

static uint8_t ladder(uint8_t ceiling, uint8_t min_bp, uint8_t out[BT_LINK_LEVELS]) {
    const uint8_t cand[BT_LINK_LEVELS] = {ceiling, BT_LINK_BITPOOL_STEP1, BT_LINK_BITPOOL_STEP2};
    uint8_t n = 0;
    for (uint8_t i = 0; i < BT_LINK_LEVELS; i++) {
        uint8_t v = clamp_bp(ceiling, min_bp, cand[i]);
        if (n == 0 || v < out[n - 1]) {
            out[n++] = v;
        }
    }
    return n;
}

uint8_t bt_link_levels(uint8_t ceiling, uint8_t min_bp) {
    uint8_t l[BT_LINK_LEVELS];
    return ladder(ceiling, min_bp, l);
}

uint8_t bt_link_bitpool(uint8_t ceiling, uint8_t min_bp, uint8_t level) {
    uint8_t l[BT_LINK_LEVELS];
    uint8_t n = ladder(ceiling, min_bp, l);
    return l[level < n ? level : n - 1u];
}

void bt_link_policy_init(bt_link_policy_t *p, uint32_t now_ms) {
    *p = (bt_link_policy_t){.window_start = now_ms, .clean_since = now_ms, .hold_ms = BT_LINK_UP_HOLD_MS};
}

uint8_t bt_link_policy_tick(bt_link_policy_t *p, uint32_t now_ms, bool pressured, uint8_t ceiling, uint8_t min_bp) {
    if (p->ticks != 0xFFFFu) {
        p->ticks++;
        p->pressured += pressured ? 1u : 0u;
    }
    if ((uint32_t)(now_ms - p->window_start) < BT_LINK_WINDOW_MS) {
        return 0;
    }
    const uint32_t ticks = p->ticks, pr = p->pressured;
    p->ticks = 0;
    p->pressured = 0;
    p->window_start = now_ms;
    const bool bad = ticks != 0 && pr * 100u >= ticks * BT_LINK_BAD_PERCENT;
    const bool clean = ticks != 0 && pr * 100u < ticks * BT_LINK_CLEAN_PERCENT;
    const uint8_t levels = bt_link_levels(ceiling, min_bp);
    const uint8_t before = bt_link_bitpool(ceiling, min_bp, p->level);

    if (bad) {
        p->clean_since = now_ms;
        if (++p->bad_windows >= BT_LINK_DOWN_WINDOWS && p->level + 1u < levels) {
            p->bad_windows = 0;
            if (p->has_up && (uint32_t)(now_ms - p->last_up) < BT_LINK_QUICK_DOWN_MS) {
                p->hold_ms = p->hold_ms * 2u < BT_LINK_UP_HOLD_MAX_MS ? p->hold_ms * 2u : BT_LINK_UP_HOLD_MAX_MS;
            } else {
                p->hold_ms = BT_LINK_UP_HOLD_MS;
            }
            p->level++;
        }
    } else {
        p->bad_windows = 0;
        if (!clean) {
            p->clean_since = now_ms; /* not clean enough to count: the hold starts again */
        } else if (p->level > 0 && (uint32_t)(now_ms - p->clean_since) >= p->hold_ms) {
            p->level--;
            p->has_up = true;
            p->last_up = now_ms;
            p->clean_since = now_ms;
        }
    }
    const uint8_t after = bt_link_bitpool(ceiling, min_bp, p->level);
    return after != before ? after : 0;
}
