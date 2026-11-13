// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_charge.h"
#include "y2_sysfs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

y2_charge_online_t y2_charge_read_online(const char *ps_root) {
    DIR *d = opendir(ps_root);
    if (d == NULL) {
        return Y2_CHARGE_ONLINE_UNKNOWN;
    }
    bool any_readable = false, any_online = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char path[640], v[16];
        snprintf(path, sizeof path, "%s/%s/type", ps_root, e->d_name);
        if (y2_sysfs_read_line(path, v, sizeof v) && strcmp(v, "Battery") == 0) {
            continue; // the battery has no "online" and is not a charger
        }
        snprintf(path, sizeof path, "%s/%s/online", ps_root, e->d_name);
        if (!y2_sysfs_read_line(path, v, sizeof v) || (strcmp(v, "0") != 0 && strcmp(v, "1") != 0)) {
            continue; // missing or not 0/1: no information
        }
        any_readable = true;
        if (v[0] == '1') {
            any_online = true;
        }
    }
    closedir(d);
    return any_online ? Y2_CHARGE_ONLINE : any_readable ? Y2_CHARGE_OFFLINE : Y2_CHARGE_ONLINE_UNKNOWN;
}

void y2_unplug_init(y2_unplug_t *u) { *u = (y2_unplug_t){0}; }

bool y2_unplug_update(y2_unplug_t *u, y2_charge_online_t online, uint32_t now_ms) {
    if (online == Y2_CHARGE_ONLINE) {
        u->seen_online = true;
    }
    if (online != Y2_CHARGE_OFFLINE || !u->seen_online) {
        u->offline = false; // online, or not proven offline: restart the 3 s
        return false;
    }
    if (!u->offline) {
        u->offline = true;
        u->since_ms = now_ms;
    }
    return (uint32_t)(now_ms - u->since_ms) >= Y2_CHARGE_UNPLUG_MS;
}

void y2_charge_init(y2_charge_t *c) {
    *c = (y2_charge_t){0};
    y2_input_init(&c->in);
    y2_unplug_init(&c->unplug);
}

// Power held Y2_CHARGE_REBOOT_HOLD_MS: noted while still held (the step below) or, when no step ran in time, at the release.
static void power_hold_check(y2_charge_t *c, uint32_t now_ms) {
    if (c->pw_down && !c->pw_fired && (uint32_t)(now_ms - c->pw_ms) >= Y2_CHARGE_REBOOT_HOLD_MS) {
        c->pw_fired = true;
        c->pw_hold = true;
    }
}

void y2_charge_feed(y2_charge_t *c, const y2_event_t *ev, uint32_t now_ms) {
    if (ev->type == Y2_EV_KEY && ev->code == Y2_KEY_POWER) {
        if (ev->value == 1 && !c->pw_down) {
            c->pw_down = true;
            c->pw_fired = false;
            c->pw_ms = now_ms;
        } else if (ev->value == 0 && c->pw_down) {
            power_hold_check(c, now_ms);
            c->pw_down = false;
        }
        return;
    }
    y2_input_feed(&c->in, ev, now_ms);
}

uint32_t y2_charge_hold_ms(const y2_charge_t *c, uint32_t now_ms) { return c->pw_down ? now_ms - c->pw_ms : 0u; }

y2_charge_action_t y2_charge_step(y2_charge_t *c, uint32_t now_ms, y2_charge_online_t online) {
    power_hold_check(c, now_ms);
    (void)y2_input_take_volume(&c->in); // side keys and the wheel mean nothing here: drain them so they cannot pile up
    hal_wheel_state ws;
    (void)y2_input_take(&c->in, &ws);

    bool hold = c->pw_hold;
    c->pw_hold = false;
    bool unplugged = y2_unplug_update(&c->unplug, online, now_ms);
    if (hold && !c->reboot_sent) {
        c->reboot_sent = true;
        return Y2_CHARGE_REBOOT;
    }
    if (unplugged && !c->poweroff_sent) {
        c->poweroff_sent = true;
        return Y2_CHARGE_POWEROFF;
    }
    return Y2_CHARGE_NONE;
}

void y2_charge_failed(y2_charge_t *c, y2_charge_action_t action, uint32_t now_ms) {
    if (action == Y2_CHARGE_REBOOT) {
        c->reboot_sent = false;
    } else if (action == Y2_CHARGE_POWEROFF) {
        c->poweroff_sent = false;
        c->unplug.offline = true; // retry after another full 3 s offline
        c->unplug.since_ms = now_ms;
    }
}
