// SPDX-License-Identifier: MPL-2.0
#define _XOPEN_SOURCE 700 // realpath()
#include "y2_sd.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "y2_sysfs.h"

// Parse the digits at *s into *out. Rejects empty, leading zero, zero, non-digits and more than 9 digits.
static bool parse_index(const char **s, unsigned long *out) {
    const char *c = *s;
    if (*c < '1' || *c > '9') {
        return false;
    }
    unsigned long v = 0;
    int n = 0;
    for (; *c >= '0' && *c <= '9'; c++) {
        if (++n > 9) {
            return false;
        }
        v = v * 10 + (unsigned long)(*c - '0');
    }
    *s = c;
    *out = v;
    return true;
}

bool y2_sd_name_allowed(const char *name) {
    if (name == NULL || strncmp(name, "mmcblk", 6) != 0) {
        return false;
    }
    const char *c = name + 6;
    unsigned long n, m;
    if (!parse_index(&c, &n)) { // N >= 1, no leading zero: mmcblk0, mmcblk01 refused
        return false;
    }
    if (*c == '\0') {
        return true;
    }
    if (*c != 'p') {
        return false;
    }
    c++;
    return parse_index(&c, &m) && *c == '\0';
}

// "mmcblk2p1" -> "mmcblk2". Caller has checked the name.
static void disk_of(const char *name, char *out, size_t cap) {
    size_t n = strchr(name, 'p') != NULL ? (size_t)(strchr(name, 'p') - name) : strlen(name);
    snprintf(out, cap, "%.*s", (int)n, name);
}

static void set_why(char *why, size_t cap, const char *msg) {
    if (why != NULL && cap > 0) {
        snprintf(why, cap, "%s", msg);
    }
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// The MMC host controller of a disk, from where its sysfs entry really lives (.../mmc_host/mmcX/...). False if the
// path cannot be resolved or has no mmc_host/mmc<digits> component: the caller then refuses (fail closed).
static bool host_of(const char *sys_block, const char *disk, unsigned long *host) {
    char link[200], real[PATH_MAX];
    snprintf(link, sizeof link, "%s/%s", sys_block, disk);
    if (realpath(link, real) == NULL) {
        return false;
    }
    const char *p = real;
    while ((p = strstr(p, "/mmc_host/mmc")) != NULL) {
        const char *c = p + sizeof "/mmc_host/mmc" - 1;
        const char *end = c;
        unsigned long v = 0;
        int n = 0;
        for (; *end >= '0' && *end <= '9'; end++) {
            if (++n > 9) {
                return false;
            }
            v = v * 10 + (unsigned long)(*end - '0');
        }
        if (n > 0 && (*end == '/' || *end == '\0')) {
            *host = v;
            return true;
        }
        p = c;
    }
    return false;
}

// A card is: name rule (N >= 1), device/type == "SD", and a controller that is not mmc0 (the eMMC's). The removable
// attribute is ignored: the MTK slot driver reports 0 for a real SD slot.
static bool disk_is_card(const char *sys_block, const char *disk, char *why, size_t why_cap) {
    char path[200], v[32], msg[96];
    if (!y2_sd_name_allowed(disk)) {
        snprintf(msg, sizeof msg, "%s is not an allowed card name: refused", disk);
        set_why(why, why_cap, msg);
        return false;
    }
    snprintf(path, sizeof path, "%s/%s/device/type", sys_block, disk);
    if (!y2_sysfs_read_line(path, v, sizeof v) || strcmp(v, "SD") != 0) {
        snprintf(msg, sizeof msg, "%s is not type SD: refused", disk);
        set_why(why, why_cap, msg);
        return false;
    }
    unsigned long host;
    if (!host_of(sys_block, disk, &host)) {
        snprintf(msg, sizeof msg, "%s: cannot determine its mmc host: refused", disk);
        set_why(why, why_cap, msg);
        return false;
    }
    if (host == 0) {
        snprintf(msg, sizeof msg, "%s is on mmc0 (the internal eMMC's host): refused", disk);
        set_why(why, why_cap, msg);
        return false;
    }
    return true;
}

static int cmp_ul(const void *a, const void *b) {
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

y2_sd_result_t y2_sd_pick(const char *sys_block, const char *dev_dir, y2_sd_pick_t *out, char *why, size_t why_cap) {
    // Every whole disk mmcblkN (N >= 1) in /sys/block, with no cap: "lowest N wins" needs them all.
    unsigned long *idx = NULL;
    size_t n = 0, cap = 0;
    DIR *d = opendir(sys_block);
    if (d != NULL) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            const char *c = e->d_name;
            unsigned long v;
            if (strncmp(c, "mmcblk", 6) != 0) {
                continue;
            }
            c += 6;
            if (parse_index(&c, &v) && *c == '\0') { // whole disks only; partitions live under them
                if (n == cap) {
                    unsigned long *bigger = realloc(idx, (cap ? cap * 2 : 8) * sizeof *idx);
                    if (bigger == NULL) {
                        free(idx);
                        closedir(d);
                        set_why(why, why_cap, "out of memory scanning /sys/block: refused");
                        return Y2_SD_REFUSED; // fail closed
                    }
                    idx = bigger;
                    cap = cap ? cap * 2 : 8;
                }
                idx[n++] = v;
            }
        }
        closedir(d);
    }
    if (n == 0) {
        free(idx);
        set_why(why, why_cap, "no mmcblk1 or higher (no card inserted)");
        return Y2_SD_ABSENT;
    }
    qsort(idx, n, sizeof idx[0], cmp_ul);

    size_t qualifying = 0;
    bool have_pick = false;
    char first_why[96] = {0};
    char nonode_disk[24] = {0};
    for (size_t i = 0; i < n; i++) {
        char disk[24], w[96] = {0};
        snprintf(disk, sizeof disk, "mmcblk%lu", idx[i]);
        if (!disk_is_card(sys_block, disk, w, sizeof w)) {
            if (first_why[0] == '\0') {
                snprintf(first_why, sizeof first_why, "%s", w);
            }
            continue;
        }
        qualifying++;
        if (have_pick) {
            continue;
        }
        char whole[40], part[40];
        snprintf(whole, sizeof whole, "%s", disk);
        snprintf(part, sizeof part, "%sp1", disk);
        const char *const cand[] = {whole, part};
        for (size_t k = 0; k < 2; k++) {
            char node[128];
            snprintf(node, sizeof node, "%s/%s", dev_dir, cand[k]);
            if (y2_sd_name_allowed(cand[k]) && exists(node)) {
                memset(out, 0, sizeof *out);
                snprintf(out->disk, sizeof out->disk, "%s", disk);
                snprintf(out->name, sizeof out->name, "%s", cand[k]);
                snprintf(out->node, sizeof out->node, "%s", node);
                have_pick = true;
                break;
            }
        }
        if (!have_pick && nonode_disk[0] == '\0') {
            snprintf(nonode_disk, sizeof nonode_disk, "%s", disk);
        }
    }
    free(idx);
    if (have_pick) {
        out->qualifying = qualifying;
        return Y2_SD_FOUND;
    }
    if (nonode_disk[0] != '\0') {
        char msg[96];
        snprintf(msg, sizeof msg, "%s is in sysfs but has no device node yet", nonode_disk);
        set_why(why, why_cap, msg);
        return Y2_SD_NO_NODE;
    }
    set_why(why, why_cap, first_why);
    return Y2_SD_REFUSED;
}

bool y2_sd_expected_devnum(const char *sys_block, const char *name, unsigned *major, unsigned *minor) {
    if (!y2_sd_name_allowed(name)) {
        return false;
    }
    char path[240], v[32], disk[24];
    disk_of(name, disk, sizeof disk);
    if (strcmp(name, disk) == 0) {
        snprintf(path, sizeof path, "%s/%s/dev", sys_block, disk);
    } else {
        snprintf(path, sizeof path, "%s/%s/%s/dev", sys_block, disk, name);
    }
    unsigned a, b;
    if (!y2_sysfs_read_line(path, v, sizeof v) || sscanf(v, "%u:%u", &a, &b) != 2) {
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
    if (!y2_sysfs_read_line(path, out, cap)) {
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

static void snap_read(const char *sys_block, const char *disk, const char *file, char *out, size_t cap) {
    char path[200];
    snprintf(path, sizeof path, "%s/%s/%s", sys_block, disk, file);
    if (!y2_sysfs_read_line(path, out, cap)) {
        out[0] = '\0';
    }
}

void y2_sd_snapshot(const char *sys_block, const char *disk, y2_sd_snapshot_t *out) {
    memset(out, 0, sizeof *out);
    snap_read(sys_block, disk, "dev", out->dev, sizeof out->dev);
    snap_read(sys_block, disk, "device/cid", out->cid, sizeof out->cid);
    y2_sd_identity(sys_block, disk, &out->id);
}

static bool snap_same(const y2_sd_snapshot_t *a, const y2_sd_snapshot_t *b) {
    return strcmp(a->dev, b->dev) == 0 && strcmp(a->cid, b->cid) == 0 && strcmp(a->id.name, b->id.name) == 0 &&
           strcmp(a->id.serial, b->id.serial) == 0 && strcmp(a->id.manfid, b->id.manfid) == 0 &&
           strcmp(a->id.oemid, b->id.oemid) == 0 && strcmp(a->id.date, b->id.date) == 0;
}

y2_sd_held_t y2_sd_held_state(const char *sys_block, const char *dev_dir, const y2_sd_pick_t *pick,
                              const y2_sd_snapshot_t *snap) {
    char node[128], w[96];
    snprintf(node, sizeof node, "%s/%s", dev_dir, pick->name);
    if (!disk_is_card(sys_block, pick->disk, w, sizeof w) || !exists(node)) {
        return Y2_SD_HELD_GONE;
    }
    y2_sd_snapshot_t now;
    y2_sd_snapshot(sys_block, pick->disk, &now);
    return snap_same(&now, snap) ? Y2_SD_HELD_SAME : Y2_SD_HELD_CHANGED;
}

// mmcblk0's device numbers: from sysfs if present, else the conventional 179:0. Returns its minor range [m0, m0 + 8).
static void emmc_devnum(const char *sys_block, unsigned *major_out, unsigned *minor_out) {
    char path[200], v[32];
    unsigned a, b;
    snprintf(path, sizeof path, "%s/mmcblk0/dev", sys_block);
    if (y2_sysfs_read_line(path, v, sizeof v) && sscanf(v, "%u:%u", &a, &b) == 2) {
        *major_out = a;
        *minor_out = b;
    } else {
        *major_out = 179;
        *minor_out = 0;
    }
}

static bool is_emmc_devnum(const char *sys_block, unsigned major_n, unsigned minor_n) {
    unsigned m, m0;
    emmc_devnum(sys_block, &m, &m0);
    return major_n == m && minor_n >= m0 && minor_n < m0 + 8;
}

bool y2_sd_check_stat(const char *sys_block, const char *name, const struct stat *st, char *why,
                      size_t why_cap) {
    if (S_ISLNK(st->st_mode)) {
        set_why(why, why_cap, "node is a symlink");
        return false;
    }
    if (!S_ISBLK(st->st_mode)) {
        set_why(why, why_cap, "node is not a block device");
        return false;
    }
    unsigned want_major, want_minor;
    if (!y2_sd_expected_devnum(sys_block, name, &want_major, &want_minor)) {
        set_why(why, why_cap, "sysfs gives no device numbers for it");
        return false;
    }
    if (major(st->st_rdev) != want_major || minor(st->st_rdev) != want_minor) {
        set_why(why, why_cap, "node's major:minor is not what sysfs says");
        return false;
    }
    if (is_emmc_devnum(sys_block, want_major, want_minor)) { // defence in depth: never the eMMC's numbers
        set_why(why, why_cap, "device numbers are the internal eMMC's");
        return false;
    }
    return true;
}

bool y2_sd_check_node(const char *sys_block, const char *node, const char *name, char *why, size_t why_cap) {
    if (!y2_sd_name_allowed(name)) {
        set_why(why, why_cap, "name not allowed");
        return false;
    }
    struct stat st;
    if (lstat(node, &st) != 0) {
        set_why(why, why_cap, "node cannot be examined");
        return false;
    }
    return y2_sd_check_stat(sys_block, name, &st, why, why_cap);
}
