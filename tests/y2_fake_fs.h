// SPDX-License-Identifier: MPL-2.0
// Tiny helpers for the Y2 tests: a throw-away directory tree standing in for /sys and /dev.
#ifndef NEUPOD_TESTS_Y2_FAKE_FS_H
#define NEUPOD_TESTS_Y2_FAKE_FS_H

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[256];

static void fs_mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/** Fresh empty root (removed with fs_destroy). */
static void fs_new(void) {
    snprintf(g_root, sizeof g_root, "/tmp/neupod-y2-test-XXXXXX");
    if (mkdtemp(g_root) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
}

static void fs_destroy(void) {
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
    if (system(cmd) != 0) {
        fprintf(stderr, "cleanup of %s failed\n", g_root);
    }
}

/** Write @p content to <root>/<rel>, creating parent directories. */
static void fs_write(const char *rel, const char *content) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_root, rel);
    char dir[512];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    *slash = '\0';
    fs_mkdirs(dir);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror(path);
        exit(2);
    }
    fputs(content, f);
    fclose(f);
}

static const char *fs_path(const char *rel, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", g_root, rel);
    return out;
}

#endif // NEUPOD_TESTS_Y2_FAKE_FS_H
