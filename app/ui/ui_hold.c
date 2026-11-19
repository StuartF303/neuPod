// SPDX-License-Identifier: MPL-2.0
#include "ui_hold.h"

void ui_hold_init(ui_hold_t *h) { *h = (ui_hold_t){0}; }

// Milliseconds the key has been down; a stamp slightly ahead of the tick clock (the host tour) counts as 0.
static uint32_t held_ms(const ui_hold_t *h, uint32_t now_ms) {
    uint32_t d = now_ms - h->down_ms;
    return d >= 0x80000000u ? 0u : d;
}

static uint32_t toggle_ms(const ui_hold_t *h) { return (uint32_t)h->toggle_s * 1000u; }

uint32_t ui_hold_set(ui_hold_t *h, bool locked) {
    if (h->locked == locked) {
        return UI_HOLD_NONE;
    }
    h->locked = locked;
    return locked ? UI_HOLD_LOCKED : UI_HOLD_UNLOCKED;
}

uint32_t ui_hold_toggle(ui_hold_t *h) { return ui_hold_set(h, !h->locked); }

bool ui_hold_toggle_s_valid(uint8_t s) { return s == 0 || s == 1 || s == 2 || s == 3 || s == 5; }

uint8_t ui_hold_toggle_s_next(uint8_t s) { return s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 3 : s == 3 ? 5 : 0; }

uint32_t ui_hold_set_toggle_s(ui_hold_t *h, uint8_t s) {
    h->toggle_s = ui_hold_toggle_s_valid(s) ? s : 0;
    return h->toggle_s == 0 ? ui_hold_set(h, false) : UI_HOLD_NONE; // with the gesture off nothing could unlock it again (the switch and the console still can)
}

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
    if (h->toggle_s != 0 && !h->toggled && held >= toggle_ms(h)) {
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
    if (h->toggle_s != 0 && !h->toggled && held >= toggle_ms(h)) {
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
        if (h->toggle_s != 0) {
            p.phase = UI_HOLD_P_RING;
            p.pct = (uint8_t)(held >= toggle_ms(h) ? 100u : held * 100u / toggle_ms(h)); /* a late poll must not wrap the byte */
            p.to_lock = !h->locked;
        } else if (can_poweroff) { // gesture off: the ring fills towards the countdown, which is where the hold does something
            p.phase = UI_HOLD_P_RING;
            p.pct = (uint8_t)(held * 100u / (UI_HOLD_SHUTDOWN_MS - UI_HOLD_COUNTDOWN_MS));
            p.to_off = true;
        }
    }
    return p;
}
