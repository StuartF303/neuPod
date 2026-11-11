// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_power.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

y2_power_status_t y2_power_parse_status(const char *text) {
    if (text == NULL) {
        return Y2_PS_UNKNOWN;
    }
    if (strcmp(text, "Charging") == 0) {
        return Y2_PS_CHARGING;
    }
    if (strcmp(text, "Discharging") == 0) {
        return Y2_PS_DISCHARGING;
    }
    if (strcmp(text, "Full") == 0) {
        return Y2_PS_FULL;
    }
    if (strcmp(text, "Not charging") == 0) {
        return Y2_PS_NOT_CHARGING;
    }
    return Y2_PS_UNKNOWN;
}

uint32_t y2_power_to_mv(long raw) {
    if (raw <= 0) {
        return 0;
    }
    return raw >= 100000 ? (uint32_t)((raw + 500) / 1000) : (uint32_t)raw;
}

static bool read_line(const char *dir, const char *file, char *out, size_t cap) {
    char path[640];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (ok) {
        out[strcspn(out, "\r\n")] = '\0';
    }
    return ok;
}

static bool read_long(const char *dir, const char *file, long *out) {
    char v[32];
    if (!read_line(dir, file, v, sizeof v)) {
        return false;
    }
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) {
        return false;
    }
    *out = n;
    return true;
}

static bool find_battery(const char *ps_root, char *dir, size_t cap) {
    DIR *d = opendir(ps_root);
    if (d == NULL) {
        return false;
    }
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        char cand[512], type[32];
        snprintf(cand, sizeof cand, "%s/%s", ps_root, e->d_name);
        if (read_line(cand, "type", type, sizeof type) && strcmp(type, "Battery") == 0) {
            found = snprintf(dir, cap, "%s", cand) < (int)cap;
        }
    }
    closedir(d);
    return found;
}

bool y2_power_read(const char *ps_root, hal_battery_status_t *out, y2_power_status_t *status) {
    *out = (hal_battery_status_t){0};
    if (status != NULL) {
        *status = Y2_PS_UNKNOWN;
    }
    char dir[512];
    if (!find_battery(ps_root, dir, sizeof dir)) {
        return false;
    }
    long cap_pct, raw_v;
    if (!read_long(dir, "capacity", &cap_pct)) {
        return false;
    }
    if (!read_long(dir, "voltage_now", &raw_v) && !read_long(dir, "batt_vol", &raw_v)) {
        return false;
    }
    if (cap_pct < 0) {
        cap_pct = 0;
    }
    if (cap_pct > 100) {
        cap_pct = 100;
    }
    char st[32] = {0};
    y2_power_status_t ps = read_line(dir, "status", st, sizeof st) ? y2_power_parse_status(st) : Y2_PS_UNKNOWN;
    long present = 1;
    (void)read_long(dir, "present", &present); // optional

    out->mv = (uint16_t)y2_power_to_mv(raw_v);
    out->soc_pct = (uint8_t)cap_pct;
    out->soc_x256 = (uint16_t)(cap_pct * 256);
    out->rate_mpct_per_h = ps == Y2_PS_CHARGING ? Y2_POWER_RATE_MPCT_PER_H : ps == Y2_PS_DISCHARGING ? -Y2_POWER_RATE_MPCT_PER_H : 0;
    out->alerts = 0;
    out->valid = true;
    out->present = present != 0 && out->mv >= HAL_BATTERY_PRESENT_MV;
    if (status != NULL) {
        *status = ps;
    }
    return true;
}
