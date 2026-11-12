// SPDX-License-Identifier: MPL-2.0
#include "ui_logic.h"

#include <string.h>

#include "hal_input.h"
#include "runtime_config.h"

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void fix_window(ui_nav_t *n) {
    int32_t max_top = n->count > n->visible ? n->count - n->visible : 0;
    if (n->sel < n->top) {
        n->top = n->sel;
    } else if (n->sel >= n->top + n->visible) {
        n->top = n->sel - n->visible + 1;
    }
    n->top = clamp_i32(n->top, 0, max_top);
}

void ui_nav_init(ui_nav_t *n, int32_t count, int32_t visible) {
    n->count = count < 0 ? 0 : count;
    n->visible = visible < 1 ? 1 : visible;
    n->sel = 0;
    n->top = 0;
}

bool ui_nav_move(ui_nav_t *n, int32_t delta) {
    if (n->count == 0) {
        return false;
    }
    int32_t old = n->sel;
    n->sel = clamp_i32(n->sel + delta, 0, n->count - 1);
    fix_window(n);
    return n->sel != old;
}

void ui_nav_set(ui_nav_t *n, int32_t sel) {
    n->sel = n->count == 0 ? 0 : clamp_i32(sel, 0, n->count - 1);
    n->top = n->sel - n->visible / 2;
    fix_window(n);
}

void ui_nav_set_count(ui_nav_t *n, int32_t count) {
    n->count = count < 0 ? 0 : count;
    n->sel = n->count == 0 ? 0 : clamp_i32(n->sel, 0, n->count - 1);
    fix_window(n);
}

void ui_accel_init(ui_accel_t *a) {
    a->residue = 0;
    a->energy = 0;
    a->last_ms = 0;
}

int32_t ui_accel_feed(ui_accel_t *a, int32_t counts, uint32_t now_ms) {
    uint32_t dt = now_ms - a->last_ms;
    a->last_ms = now_ms;
    // Linear decay of the recent-movement energy over the window.
    if (dt >= UI_ACCEL_WINDOW_MS) {
        a->energy = 0;
    } else {
        a->energy = a->energy * (UI_ACCEL_WINDOW_MS - dt) / UI_ACCEL_WINDOW_MS;
    }
    a->energy += (uint32_t)(counts < 0 ? -counts : counts);
    int32_t mult = a->energy >= UI_ACCEL_T3 ? 8 : a->energy >= UI_ACCEL_T2 ? 4 : a->energy >= UI_ACCEL_T1 ? 2 : 1;
    // A reversal drops the leftover counts of the old direction.
    if ((counts > 0 && a->residue < 0) || (counts < 0 && a->residue > 0)) {
        a->residue = 0;
    }
    a->residue += counts * mult;
    int32_t steps = a->residue / UI_COUNTS_PER_STEP; // truncates toward zero
    a->residue -= steps * UI_COUNTS_PER_STEP;
    return steps;
}

void ui_stack_init(ui_stack_t *s) {
    s->depth = 0;
    for (int i = 0; i < UI_STACK_MAX; i++) {
        s->items[i] = 0;
    }
}

bool ui_stack_push(ui_stack_t *s, void *item) {
    if (s->depth >= UI_STACK_MAX || item == 0) {
        return false;
    }
    s->items[s->depth++] = item;
    return true;
}

void *ui_stack_pop(ui_stack_t *s) {
    if (s->depth == 0) {
        return 0;
    }
    void *it = s->items[--s->depth];
    s->items[s->depth] = 0;
    return it;
}

void *ui_stack_top(const ui_stack_t *s) { return s->depth ? s->items[s->depth - 1] : 0; }

void *ui_stack_below_top(const ui_stack_t *s) { return s->depth >= 2 ? s->items[s->depth - 2] : 0; }

/* ---- Signal meter ---- */

const ui_signal_thr_t k_ui_signal_wifi = {UI_SIG_WIFI_4_DBM, UI_SIG_WIFI_3_DBM, UI_SIG_WIFI_2_DBM};
const ui_signal_thr_t k_ui_signal_bt = {UI_SIG_BT_4_DBM, UI_SIG_BT_3_DBM, UI_SIG_BT_2_DBM};

/* The RSSI at which @p bars (2..4) starts. */
static int32_t bar_floor(const ui_signal_thr_t *t, int bars) { return bars >= 4 ? t->b4 : bars == 3 ? t->b3 : t->b2; }

static bool rssi_ge_zero(int32_t rssi) { return rssi >= 0; }

int ui_signal_bars(int32_t rssi, const ui_signal_thr_t *thr, int prev, int32_t hyst) {
    if (rssi_ge_zero(rssi)) {
        return prev >= 1 && prev <= 4 ? prev : 0;
    }
    if (prev < 1 || prev > 4) {
        prev = 0;
        hyst = 0;
    }
    int bars = prev != 0 ? prev : 1;
    while (bars < 4 && rssi >= bar_floor(thr, bars + 1) + hyst) {
        bars++;
    }
    while (bars > 1 && rssi < bar_floor(thr, bars) - hyst) {
        bars--;
    }
    return bars;
}

int ui_signal_class(int bars) { return bars >= 3 ? 3 : bars == 2 ? 2 : 1; }

const char *ui_signal_word(int bars) { return bars <= 0 ? "-" : bars >= 4 ? "Excellent" : bars == 3 ? "Good" : bars == 2 ? "Fair" : "Weak"; }

/* ---- Backlight auto-dim -------------------------------------------------------------------------- */

static const struct {
    uint16_t sec;
    const char *name;
} k_autodim[UI_AUTODIM_STEPS] = {{10, "10 s"}, {15, "15 s"}, {30, "30 s"}, {60, "1 min"}, {120, "2 min"}, {300, "5 min"}};

uint32_t ui_autodim_seconds(uint8_t idx) { return idx < UI_AUTODIM_STEPS ? k_autodim[idx].sec : 0u; }
const char *ui_autodim_name(uint8_t idx) { return idx < UI_AUTODIM_STEPS ? k_autodim[idx].name : "-"; }

void ui_idle_init(ui_idle_t *t, uint32_t now_ms) {
    t->last_ms = now_ms;
    t->dimmed = false;
}

bool ui_idle_activity(ui_idle_t *t, uint32_t now_ms) {
    bool was = t->dimmed;
    t->last_ms = now_ms;
    t->dimmed = false;
    return was;
}

bool ui_idle_tick(ui_idle_t *t, uint32_t now_ms, bool enabled, bool inhibit, uint32_t timeout_ms) {
    if (inhibit) {
        t->last_ms = now_ms; /* the timer restarts when the inhibit ends */
    }
    uint32_t elapsed = now_ms - t->last_ms;
    bool future = elapsed >= 0u - UI_IDLE_FUTURE_MS; /* the last input is slightly ahead of this tick (host tour) */
    bool dim = enabled && !future && elapsed >= timeout_ms;
    bool changed = dim != t->dimmed;
    t->dimmed = dim;
    return changed;
}

uint8_t ui_backlight_effective(uint8_t set_pct, bool dimmed) { return dimmed && set_pct > UI_DIM_PCT ? (uint8_t)UI_DIM_PCT : set_pct; }

/* ---- Global transport keys ------------------------------------------------------------------------ */

ui_key_action_t ui_global_key(uint8_t btn, bool queue_active) {
    switch (btn) {
    case HAL_BTN_MENU: return UI_KEY_BACK;
    case HAL_BTN_PLAY: return UI_KEY_PAUSE;
    case HAL_BTN_RIGHT: return queue_active ? UI_KEY_NEXT : UI_KEY_NONE;
    case HAL_BTN_LEFT: return queue_active ? UI_KEY_PREV : UI_KEY_NONE;
    default: return UI_KEY_NONE;
    }
}

/* ---- Now Playing controls --------------------------------------------------------------------------- */

static bool elapsed_ge(uint32_t now_ms, uint32_t since_ms, uint32_t ms) { return (uint32_t)(now_ms - since_ms) >= ms; }

void ui_npc_init(ui_npc_t *c) { memset(c, 0, sizeof *c); }

uint32_t ui_seek_step_s(uint64_t total_s) {
    uint64_t s = total_s / 60u;
    return s < 1u ? 1u : s > 30u ? 30u : (uint32_t)s;
}

uint64_t ui_npc_position(const ui_npc_t *c, const ui_npc_track_t *t) { return c->pending ? c->target : t->pos_frames; }

// Move the target by @p delta_frames from where it is now (the playing position when none is pending), clamped to the track.
static void move_target(ui_npc_t *c, const ui_npc_track_t *t, int64_t delta_frames, uint32_t now_ms) {
    if (c->pending && c->id != t->id) {
        c->pending = false; /* a scrub of the track before: it starts again from where this one plays */
    }
    int64_t base = (int64_t)ui_npc_position(c, t);
    int64_t to = base + delta_frames;
    if (to < 0) {
        to = 0;
    }
    if (to > (int64_t)t->total_frames) {
        to = (int64_t)t->total_frames; /* the end itself: applying it is the end of the track */
    }
    c->target = (uint64_t)to;
    c->pending = true;
    c->id = t->id;
    c->move_ms = now_ms;
}

bool ui_npc_select(ui_npc_t *c, const ui_npc_track_t *t, uint32_t now_ms) {
    if (c->mode == UI_WHEEL_SEEK) {
        c->mode = UI_WHEEL_VOLUME; /* a scrub still waiting is applied by the next tick */
        return true;
    }
    if (!t->seekable || t->rate == 0 || t->total_frames == 0) {
        return false;
    }
    c->mode = UI_WHEEL_SEEK;
    c->mode_ms = now_ms;
    return true;
}

bool ui_npc_scroll(ui_npc_t *c, const ui_npc_track_t *t, int32_t steps, uint32_t now_ms) {
    if (c->mode != UI_WHEEL_SEEK) {
        return false;
    }
    c->mode_ms = now_ms;
    if (t->seekable && t->rate != 0 && t->total_frames != 0) {
        move_target(c, t, (int64_t)steps * ui_seek_step_s(t->total_frames / t->rate) * t->rate, now_ms);
    }
    return true;
}

void ui_npc_cancel_hold(ui_npc_t *c) {
    if (c->holding) {
        c->pending = false; /* the fast-forward was not finished by a release: nothing is applied */
    }
    c->hold_dir = 0;
    c->holding = false;
}

bool ui_npc_commit_valid(const ui_npc_t *c, const ui_npc_track_t *t) { return c->pending && c->id == t->id; }

void ui_npc_key_down(ui_npc_t *c, int dir, const ui_npc_track_t *t, uint32_t now_ms) {
    ui_npc_cancel_hold(c); /* a second key while one is down: the first one's release may never be seen */
    c->hold_id = t->id;
    c->hold_dir = dir < 0 ? -1 : 1;
    c->holding = false;
    c->hold_ms = now_ms;
    c->step_ms = now_ms;
}

uint32_t ui_npc_key_up(ui_npc_t *c, int dir, uint32_t now_ms) {
    (void)now_ms;
    int8_t d = dir < 0 ? -1 : 1;
    if (c->hold_dir != d) {
        return UI_NPC_NONE; /* its press was not seen here */
    }
    bool was_hold = c->holding;
    c->hold_dir = 0;
    c->holding = false;
    if (!was_hold) {
        return UI_NPC_SKIP;
    }
    return c->pending ? UI_NPC_COMMIT : UI_NPC_NONE;
}

uint32_t ui_npc_tick(ui_npc_t *c, const ui_npc_track_t *t, uint32_t now_ms) {
    uint32_t ev = UI_NPC_NONE;
    bool can = t->seekable && t->rate != 0 && t->total_frames != 0;
    if (c->pending && c->id != t->id) {
        c->pending = false; /* the track changed (a gapless handoff, a skip) under a scrub or a hold: it does not apply to the new one */
    }
    if (c->hold_dir != 0 && c->hold_id != t->id) {
        ui_npc_cancel_hold(c); /* its release does nothing either: no skip, no seek */
    }
    if (c->hold_dir != 0 && !c->holding && elapsed_ge(now_ms, c->hold_ms, UI_HOLD_MS)) {
        c->holding = true;
        c->step_ms = now_ms;
        if (!can) {
            ev |= UI_NPC_UNAVAILABLE;
        }
    }
    if (c->holding && can) {
        uint32_t dt = (uint32_t)(now_ms - c->step_ms);
        if (dt != 0) {
            c->step_ms = now_ms;
            move_target(c, t, (int64_t)c->hold_dir * (int64_t)((uint64_t)t->rate * UI_FF_SPEED * dt / 1000u), now_ms);
        }
        // The hold ends itself at the start or the end of the track (a fast-forward to the end is the end of the track) and after UI_HOLD_MAX_MS:
        // what was reached is applied now, and the key's release (if it ever comes) does nothing.
        if (c->pending && (c->target == 0 || c->target >= t->total_frames)) {
            c->hold_dir = 0;
            c->holding = false;
            return ev | UI_NPC_COMMIT;
        }
    }
    if (c->holding && elapsed_ge(now_ms, c->hold_ms, UI_HOLD_MAX_MS)) {
        c->hold_dir = 0;
        c->holding = false;
        return ev | (c->pending ? UI_NPC_COMMIT : UI_NPC_NONE);
    }
    if (c->mode == UI_WHEEL_SEEK && (!can || (!c->holding && !c->pending && elapsed_ge(now_ms, c->mode_ms, UI_SEEK_IDLE_MS)))) {
        c->mode = UI_WHEEL_VOLUME;
        ev |= UI_NPC_TO_VOLUME;
    }
    if (c->pending && !c->holding && can && elapsed_ge(now_ms, c->move_ms, UI_SEEK_COMMIT_MS)) {
        ev |= UI_NPC_COMMIT;
    }
    if (c->pending && !can) {
        c->pending = false; /* the track changed under a scrub: drop it */
    }
    return ev;
}

void ui_npc_committed(ui_npc_t *c) {
    c->pending = false;
    c->mode_ms = c->move_ms; /* the idle clock counts from the last input, not from the commit */
}

void ui_display_name(char *out, size_t n, const char *raw) {
    if (n == 0) {
        return;
    }
    size_t o = 0;
    const unsigned char *r = (const unsigned char *)raw;
    while (*r != '\0') {
        const char *rep = NULL;
        size_t len = 1;
        if (r[0] == 0xE2u && r[1] == 0x80u && r[2] != 0) {
            len = 3;
            switch (r[2]) {
            case 0x98: case 0x99: rep = "'"; break;
            case 0x9C: case 0x9D: rep = "\""; break;
            case 0x93: case 0x94: rep = "-"; break;
            case 0xA6: rep = "..."; break;
            default: break;
            }
        } else if (r[0] >= 0xC0u) {
            len = r[0] >= 0xF0u ? 4 : r[0] >= 0xE0u ? 3 : 2;
        }
        for (size_t i = 1; i < len; i++) { // a truncated sequence at the end of the string: stop at the NUL
            if ((r[i] & 0xC0u) != 0x80u) {
                len = i;
                break;
            }
        }
        size_t w = rep != NULL ? strlen(rep) : len;
        if (o + w + 1 > n) {
            break; // never cut inside a character
        }
        memcpy(out + o, rep != NULL ? (const unsigned char *)rep : r, w);
        o += w;
        r += len;
    }
    out[o] = '\0';
}
