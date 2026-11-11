// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_input.h"
#include "y2_sysfs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

size_t y2_evdev_decode(const uint8_t *buf, size_t len, y2_event_t *out, size_t max, size_t *consumed) {
    size_t n = 0;
    size_t off = 0;
    while (off + Y2_EVENT_SIZE <= len && n < max) {
        const uint8_t *r = buf + off;
        out[n].type = (uint16_t)(r[8] | (r[9] << 8));
        out[n].code = (uint16_t)(r[10] | (r[11] << 8));
        out[n].value = (int32_t)((uint32_t)r[12] | ((uint32_t)r[13] << 8) | ((uint32_t)r[14] << 16) | ((uint32_t)r[15] << 24));
        n++;
        off += Y2_EVENT_SIZE;
    }
    if (consumed != NULL) {
        *consumed = off;
    }
    return n;
}

void y2_input_init(y2_input_t *s) {
    memset(s, 0, sizeof *s);
    s->dirty = true; // the first poll reports a frame: the wheel boot check waits for one
}

static uint8_t ring_button(uint16_t code) {
    switch (code) {
    case Y2_KEY_ENTER: return HAL_BTN_SELECT;
    case Y2_KEY_BACK: return HAL_BTN_MENU;
    case Y2_KEY_LEFT: return HAL_BTN_LEFT;
    case Y2_KEY_RIGHT: return HAL_BTN_RIGHT;
    case Y2_KEY_PLAYPAUSE: return HAL_BTN_PLAY;
    default: return 0;
    }
}

static void move(y2_input_t *s, int detents) {
    int pos = ((int)s->position + detents * Y2_POSITIONS_PER_DETENT) % HAL_WHEEL_POSITIONS;
    if (pos < 0) {
        pos += HAL_WHEEL_POSITIONS;
    }
    s->position = (uint8_t)pos;
    s->dirty = true;
}

void y2_input_feed(y2_input_t *s, const y2_event_t *ev, uint32_t now_ms) {
    if (ev->type != Y2_EV_KEY) {
        return;
    }
    const uint16_t code = ev->code;
    const int32_t value = ev->value; // 1 press, 0 release, 2 autorepeat
    uint8_t btn = ring_button(code);
    if (btn != 0) {
        if (value == 1) {
            s->buttons = (uint8_t)(s->buttons | btn);
            s->latched = (uint8_t)(s->latched | btn);
            if (btn == HAL_BTN_SELECT || btn == HAL_BTN_LEFT || btn == HAL_BTN_RIGHT) {
                s->have_press = true; // the brush filter's reference (not Menu / Play, see Y2_TICK_AFTER_PRESS_IGNORE_MS)
                s->last_press_ms = now_ms;
            }
            s->dirty = true;
        } else if (value == 0 && (s->buttons & btn) != 0) {
            s->buttons = (uint8_t)(s->buttons & ~btn);
            s->dirty = true;
        }
        return; // autorepeat of a held button is not a new press
    }
    switch (code) {
    case Y2_KEY_DOWN:
    case Y2_KEY_UP:
        if (value != 1) {
            return; // one tick = the press; the release carries nothing
        }
        if (s->have_press && (uint32_t)(now_ms - s->last_press_ms) < Y2_TICK_AFTER_PRESS_IGNORE_MS) {
            s->ticks_dropped++;
            return;
        }
        move(s, code == Y2_KEY_DOWN ? 1 : -1);
        break;
    case Y2_KEY_VOLUMEUP:
    case Y2_KEY_VOLUMEDOWN:
        if (value == 1 || value == 2) { // holding the key repeats
            s->volume_steps += code == Y2_KEY_VOLUMEUP ? 1 : -1;
        }
        break;
    case Y2_KEY_POWER:
        if (value == 1) {
            s->power_presses++;
        }
        break;
    default:
        break;
    }
}

bool y2_input_take(y2_input_t *s, hal_wheel_state *out) {
    uint8_t shown = (uint8_t)(s->buttons | s->latched);
    if (out != NULL) {
        *out = (hal_wheel_state){.buttons = shown, .position = s->position, .touched = true, .valid = true};
    }
    bool was = s->dirty;
    s->dirty = (shown != s->buttons); // a tap that is already over: the next take must show the release
    s->latched = 0;
    return was;
}

int32_t y2_input_take_volume(y2_input_t *s) {
    int32_t v = s->volume_steps;
    s->volume_steps = 0;
    return v;
}

bool y2_input_find_node(const char *class_dir, const char *dev_dir, const char *name, char *out, size_t cap) {
    DIR *d = opendir(class_dir);
    if (d == NULL) {
        return false;
    }
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0 || e->d_name[5] == '\0') {
            continue;
        }
        char path[320];
        snprintf(path, sizeof path, "%s/%s/device/name", class_dir, e->d_name);
        char line[96];
        if (y2_sysfs_read_line(path, line, sizeof line) && strcmp(line, name) == 0) {
            found = snprintf(out, cap, "%s/%s", dev_dir, e->d_name) < (int)cap;
        }
    }
    closedir(d);
    return found;
}

int y2_console_key(int c, bool from_tty) {
    if (!from_tty && (c == '\n' || c == '\r')) {
        return -1;
    }
    return c;
}
