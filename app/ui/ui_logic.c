// SPDX-License-Identifier: MPL-2.0
#include "ui_logic.h"
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
