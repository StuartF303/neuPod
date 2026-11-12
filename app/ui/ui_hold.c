// SPDX-License-Identifier: MPL-2.0
#include "ui_hold.h"

void ui_hold_init(ui_hold_t *h) { *h = (ui_hold_t){0}; }

// Milliseconds the key has been down; a stamp slightly ahead of the tick clock (the host tour) counts as 0.
static uint32_t held_ms(const ui_hold_t *h, uint32_t now_ms) {
    uint32_t d = now_ms - h->down_ms;
    return d >= 0x80000000u ? 0u : d;
}

uint32_t ui_hold_set(ui_hold_t *h, bool locked) {
    if (h->locked == locked) {
        return UI_HOLD_NONE;
    }
    h->locked = locked;
    return locked ? UI_HOLD_LOCKED : UI_HOLD_UNLOCKED;
}

uint32_t ui_hold_toggle(ui_hold_t *h) { return ui_hold_set(h, !h->locked); }

uint32_t ui_hold_power_down(ui_hold_t *h, uint32_t now_ms, bool woke) {
    if (!h->down) {
        h->down = true;
        h->down_ms = now_ms;
        h->woke = woke;
        h->toggled = false;
        h->poweroff_sent = false;
    }
    return UI_HOLD_NONE;
}

void ui_hold_power_lost(ui_hold_t *h) {
    h->down = false;
    h->toggled = false;
    h->poweroff_sent = false;
    h->woke = false;
}

uint32_t ui_hold_tick(ui_hold_t *h, uint32_t now_ms, bool can_poweroff) {
    uint32_t r = UI_HOLD_NONE;
    if (!h->down) {
        return r;
    }
    uint32_t held = held_ms(h, now_ms);
    if (!h->toggled && held >= UI_HOLD_TOGGLE_MS) {
        h->toggled = true;
        r |= ui_hold_toggle(h);
    }
    if (can_poweroff && !h->poweroff_sent && held >= UI_HOLD_SHUTDOWN_MS) {
        h->poweroff_sent = true;
        r |= UI_HOLD_POWEROFF;
    }
    return r;
}

uint32_t ui_hold_power_up(ui_hold_t *h, uint32_t now_ms) {
    if (!h->down) {
        return UI_HOLD_NONE;
    }
    uint32_t r = UI_HOLD_NONE;
    uint32_t held = held_ms(h, now_ms);
    if (!h->toggled && held >= UI_HOLD_TOGGLE_MS) {
        r |= ui_hold_toggle(h); // a poll gap longer than the hold time still counts as a hold
        h->toggled = true;
    }
    if (!h->toggled && !h->woke && held < UI_HOLD_RING_DELAY_MS) {
        r |= UI_HOLD_SLEEP; // a tap; a press long enough to have shown the ring was an aborted hold: it does nothing
    }
    if (held >= UI_HOLD_RING_DELAY_MS) {
        r |= UI_HOLD_CANCELLED; // a ring or a countdown may be on screen: it goes away with the key
    }
    h->down = false;
    h->woke = false;
    h->toggled = false;
    h->poweroff_sent = false;
    return r;
}

ui_hold_progress_t ui_hold_progress(const ui_hold_t *h, uint32_t now_ms, bool can_poweroff) {
    ui_hold_progress_t p = {0};
    if (!h->down) {
        return p;
    }
    uint32_t held = held_ms(h, now_ms);
    if (h->poweroff_sent) {
        p.phase = UI_HOLD_P_POWEROFF;
        p.pct = 100;
        return p;
    }
    if (can_poweroff && held >= UI_HOLD_SHUTDOWN_MS - UI_HOLD_COUNTDOWN_MS) {
        uint32_t left = held >= UI_HOLD_SHUTDOWN_MS ? 0u : UI_HOLD_SHUTDOWN_MS - held;
        p.phase = UI_HOLD_P_COUNTDOWN;
        p.secs = (uint8_t)((left + 999u) / 1000u);
        p.pct = (uint8_t)(left * 100u / UI_HOLD_COUNTDOWN_MS);
        return p;
    }
    if (!h->toggled && held >= UI_HOLD_RING_DELAY_MS) {
        p.phase = UI_HOLD_P_RING;
        p.pct = (uint8_t)(held * 100u / UI_HOLD_TOGGLE_MS);
        p.to_lock = !h->locked;
    }
    return p;
}
