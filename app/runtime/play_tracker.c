// SPDX-License-Identifier: MPL-2.0
#include "play_tracker.h"

void ptrk_init(ptrk_t *t) {
    t->have = false;
    t->track_id = 0;
    t->played_ms = 0;
    t->last_ms = 0;
    t->counted = false;
}

bool ptrk_update(ptrk_t *t, uint32_t now_ms, bool playing, bool ended, uint32_t track_id, uint32_t duration_ms) {
    if (!t->have || track_id != t->track_id) {
        t->have = true;
        t->track_id = track_id;
        t->played_ms = 0;
        t->counted = false;
        t->last_ms = now_ms;
    }
    uint32_t dt = now_ms - t->last_ms; /* wrap-safe */
    t->last_ms = now_ms;
    if (playing) {
        t->played_ms += dt > PTRK_MAX_STEP_MS ? PTRK_MAX_STEP_MS : dt;
    }
    if (t->counted) {
        return false;
    }
    bool half = duration_ms != 0 && (uint64_t)t->played_ms * 2u >= duration_ms;
    bool long_enough = duration_ms == 0 && t->played_ms >= PTRK_UNKNOWN_LEN_MS;
    bool end = ended && t->played_ms >= PTRK_MIN_END_MS;
    if (half || long_enough || end) {
        t->counted = true;
        return true;
    }
    return false;
}
