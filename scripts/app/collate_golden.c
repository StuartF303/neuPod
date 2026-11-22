// SPDX-License-Identifier: MPL-2.0
// Prints lib_collate_key() for each input line (hex-encoded "name" or "name\tsort"),
// so the app's Dart port can be checked byte for byte against the firmware.
// Built and run by scripts/app/gen-collate-golden.sh; reads stdin, writes
// "<hex input>\t<hex sort or ->\t<hex key>" lines.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "library.h"

static size_t unhex(const char *h, size_t len, char *out) {
    size_t n = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned v;
        sscanf(h + i, "%2x", &v);
        out[n++] = (char)v;
    }
    out[n] = '\0';
    return n;
}

int main(void) {
    char line[4096], name[2048], sort[2048];
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strcspn(line, "\r\n");
        line[len] = '\0';
        char *tab = strchr(line, '\t');
        const char *sp = NULL;
        if (tab) {
            unhex(line, (size_t)(tab - line), name);
            if (strcmp(tab + 1, "-") != 0) {
                unhex(tab + 1, strlen(tab + 1), sort);
                sp = sort;
            }
        } else {
            unhex(line, len, name);
        }
        uint8_t key[LIB_KEY_MAX];
        size_t n = lib_collate_key(name, sp, key, sizeof key);
        printf("%s\t", line);
        if (!tab) printf("-\t");
        for (size_t i = 0; i < n; i++) printf("%02x", key[i]);
        printf("\n");
    }
    return 0;
}
