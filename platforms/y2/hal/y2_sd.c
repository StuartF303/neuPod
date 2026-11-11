// SPDX-License-Identifier: MPL-2.0
#define _POSIX_C_SOURCE 200809L
#include "y2_sd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define Y2_SD_DISK "mmcblk1"

bool y2_sd_name_allowed(const char *name) {
    if (name == NULL || strncmp(name, Y2_SD_DISK, sizeof Y2_SD_DISK - 1) != 0) {
        return false;
    }
    const char *rest = name + sizeof Y2_SD_DISK - 1;
    if (*rest == '\0') {
        return true;
    }
    if (*rest != 'p' || rest[1] == '\0') {
        return false;
    }
    for (const char *c = rest + 1; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            return false;
        }
    }
    return true;
}

static void set_why(char *why, size_t cap, const char *fmt, const char *a) {
    if (why != NULL && cap > 0) {
        snprintf(why, cap, fmt, a);
    }
}

// First line of a small sysfs file, trailing whitespace removed. False if it cannot be read.
static bool read_line(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    bool ok = fgets(out, (int)cap, f) != NULL;
    fclose(f);
    if (ok) {
        out[strcspn(out, "\r\n")] = '\0';
    } else {
        out[0] = '\0';
    }
    return ok;
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool disk_is_card(const char *sys_block, char *why, size_t why_cap) {
    char path[160], v[32];
    snprintf(path, sizeof path, "%s/%s/removable", sys_block, Y2_SD_DISK);
    if (read_line(path, v, sizeof v) && strcmp(v, "1") == 0) {
        return true;
    }
    snprintf(path, sizeof path, "%s/%s/device/type", sys_block, Y2_SD_DISK);
    if (read_line(path, v, sizeof v) && strcmp(v, "SD") == 0) {
        return true;
    }
    set_why(why, why_cap, "%s is neither removable nor type SD: refused", Y2_SD_DISK);
    return false;
}

y2_sd_result_t y2_sd_pick(const char *sys_block, const char *dev_dir, y2_sd_pick_t *out, char *why, size_t why_cap) {
    char path[160];
    snprintf(path, sizeof path, "%s/%s", sys_block, Y2_SD_DISK);
    if (!exists(path)) {
        set_why(why, why_cap, "no %s (no card inserted)", Y2_SD_DISK);
        return Y2_SD_ABSENT;
    }
    if (!disk_is_card(sys_block, why, why_cap)) {
        return Y2_SD_REFUSED;
    }
    static const char *const k_candidates[] = {"mmcblk1", "mmcblk1p1"};
    for (size_t i = 0; i < sizeof k_candidates / sizeof k_candidates[0]; i++) {
        if (!y2_sd_name_allowed(k_candidates[i])) { // belt and braces: the table above must obey the rule
            continue;
        }
        char node[128];
        snprintf(node, sizeof node, "%s/%s", dev_dir, k_candidates[i]);
        if (exists(node)) {
            memset(out, 0, sizeof *out);
            snprintf(out->disk, sizeof out->disk, "%s", Y2_SD_DISK);
            snprintf(out->name, sizeof out->name, "%s", k_candidates[i]);
            snprintf(out->node, sizeof out->node, "%s", node);
            return Y2_SD_FOUND;
        }
    }
    set_why(why, why_cap, "%s is in sysfs but has no device node yet", Y2_SD_DISK);
    return Y2_SD_NO_NODE;
}

bool y2_sd_expected_devnum(const char *sys_block, const char *name, unsigned *major, unsigned *minor) {
    if (!y2_sd_name_allowed(name)) {
        return false;
    }
    char path[200], v[32];
    if (strcmp(name, Y2_SD_DISK) == 0) {
        snprintf(path, sizeof path, "%s/%s/dev", sys_block, Y2_SD_DISK);
    } else {
        snprintf(path, sizeof path, "%s/%s/%s/dev", sys_block, Y2_SD_DISK, name);
    }
    unsigned a, b;
    if (!read_line(path, v, sizeof v) || sscanf(v, "%u:%u", &a, &b) != 2) {
        return false;
    }
    *major = a;
    *minor = b;
    return true;
}

bool y2_sd_devnum_matches(const char *sys_block, const char *name, unsigned major, unsigned minor) {
    unsigned a, b;
    return y2_sd_expected_devnum(sys_block, name, &a, &b) && a == major && b == minor;
}

static void ident(const char *sys_block, const char *disk, const char *file, char *out, size_t cap) {
    char path[200];
    snprintf(path, sizeof path, "%s/%s/device/%s", sys_block, disk, file);
    if (!read_line(path, out, cap)) {
        out[0] = '\0';
    }
}

void y2_sd_identity(const char *sys_block, const char *disk, y2_sd_identity_t *out) {
    memset(out, 0, sizeof *out);
    ident(sys_block, disk, "name", out->name, sizeof out->name);
    ident(sys_block, disk, "serial", out->serial, sizeof out->serial);
    ident(sys_block, disk, "manfid", out->manfid, sizeof out->manfid);
    ident(sys_block, disk, "oemid", out->oemid, sizeof out->oemid);
    ident(sys_block, disk, "date", out->date, sizeof out->date);
}
