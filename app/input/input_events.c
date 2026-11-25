// SPDX-License-Identifier: MPL-2.0
#include "input_events.h"

size_t input_events_diff(const hal_wheel_state *prev, const hal_wheel_state *cur,
                         input_event_t *out, size_t max) {
    size_t n = 0;
    if (!cur->valid) {
        return 0;
    }
    uint8_t before = prev->valid ? prev->buttons : 0;
    uint8_t changed = (uint8_t)(before ^ cur->buttons);
    for (uint8_t bit = 1; bit != 0 && bit <= HAL_BTN_MENU; bit = (uint8_t)(bit << 1)) {
        if ((changed & bit) != 0 && n < max) {
            out[n].type = (cur->buttons & bit) != 0 ? INPUT_BUTTON_DOWN : INPUT_BUTTON_UP;
            out[n].button = bit;
            out[n].steps = 0;
            n++;
        }
    }
    if (prev->valid && prev->touched && cur->touched && n < max) {
        int delta = (int)cur->position - (int)prev->position;
        if (delta > HAL_WHEEL_POSITIONS / 2) {
            delta -= HAL_WHEEL_POSITIONS;
        } else if (delta < -(HAL_WHEEL_POSITIONS / 2)) {
            delta += HAL_WHEEL_POSITIONS;
        }
        if (delta != 0) {
            out[n].type = INPUT_SCROLL;
            out[n].button = 0;
            out[n].steps = (int8_t)delta;
            n++;
        }
    }
    return n;
}

size_t input_events_from_key(int key, input_event_t *out, size_t max) {
    uint8_t button = 0;
    int8_t steps = 0;
    switch (key) {
    case 'w':
    case 'k':
        steps = -4;
        break;
    case 's':
    case 'j':
        steps = 4;
        break;
    case '\r':
    case '\n':
    case 'e':
        button = HAL_BTN_SELECT;
        break;
    case 'a':
        button = HAL_BTN_LEFT;
        break;
    case 'd':
        button = HAL_BTN_RIGHT;
        break;
    case 'p':
    case ' ':
        button = HAL_BTN_PLAY;
        break;
    case 'l':
        if (max < 1) {
            return 0;
        }
        out[0] = (input_event_t){.type = INPUT_HOLD_TOGGLE};
        return 1;
    case 'm':
    case 0x7f:
    case 0x08:
        button = HAL_BTN_MENU;
        break;
    default:
        return 0;
    }
    size_t n = 0;
    if (steps != 0 && max >= 1) {
        out[n++] = (input_event_t){.type = INPUT_SCROLL, .button = 0, .steps = steps};
    } else if (button != 0 && max >= 2) {
        out[n++] = (input_event_t){.type = INPUT_BUTTON_DOWN, .button = button, .steps = 0};
        out[n++] = (input_event_t){.type = INPUT_BUTTON_UP, .button = button, .steps = 0};
    }
    return n;
}

void input_pump_init(input_pump_t *p) { *p = (input_pump_t){0}; }

void input_pump_send(input_pump_t *p, input_send_fn send, void *ctx, const input_event_t *ev, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!send(ctx, &ev[i])) {
            p->dropped += (uint32_t)(n - i);
            return; // the queue is full: the rest of this burst would not fit either
        }
    }
}

void input_pump_levels(input_pump_t *p, input_send_fn send, void *ctx, bool power_down, hal_hold_switch_t sw) {
    if (power_down != p->power_sent) {
        input_event_t e = {.type = power_down ? INPUT_POWER_DOWN : INPUT_POWER_UP};
        if (send(ctx, &e)) {
            p->power_sent = power_down;
        }
    }
    if (sw != HAL_HOLD_SWITCH_NONE && sw != p->switch_sent) {
        input_event_t e = {.type = INPUT_HOLD_SWITCH, .steps = sw == HAL_HOLD_SWITCH_ON ? 1 : 0};
        if (send(ctx, &e)) {
            p->switch_sent = sw;
        }
    }
}
