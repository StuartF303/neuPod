// SPDX-License-Identifier: MPL-2.0
// The on-device Licences list (app/ui/ui_licence_data.c) against LICENSE.md: every component the screen lists is named in LICENSE.md, on a row that also
// carries its licence; notices are short enough to be drawn without scrolling; the Y2 does not list what only the RP2350 firmware contains.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_licence_data.h"
#include "unit.h"

static char *g_md;

static bool load_licence_md(void) {
    FILE *f = fopen(NEUPOD_LICENSE_MD, "rb");
    if (f == NULL) {
        printf("  cannot open %s\n", NEUPOD_LICENSE_MD);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_md = malloc((size_t)n + 1);
    size_t got = g_md != NULL ? fread(g_md, 1, (size_t)n, f) : 0;
    fclose(f);
    if (g_md == NULL || got != (size_t)n) {
        return false;
    }
    g_md[n] = '\0';
    return true;
}

// The line of LICENSE.md that contains @p key, copied into @p line; false if there is none.
static bool row_of(const char *key, char *line, size_t cap) {
    const char *p = g_md;
    while ((p = strstr(p, key)) != NULL) {
        const char *b = p;
        while (b > g_md && b[-1] != '\n') b--;
        const char *e = strchr(p, '\n');
        size_t n = e != NULL ? (size_t)(e - b) : strlen(b);
        if (b[0] == '|') { // a table row, not a sentence of the prose around it
            snprintf(line, cap, "%.*s", (int)(n < cap - 1 ? n : cap - 1), b);
            return true;
        }
        p += strlen(key);
    }
    return false;
}

TEST(every_listed_component_is_in_licence_md_with_its_licence) {
    ASSERT_TRUE(load_licence_md());
    size_t n;
    const ui_licence_t *l = ui_licence_all(&n);
    ASSERT_TRUE(n >= 14);
    static char row[8192];
    for (size_t i = 0; i < n; i++) {
        bool found = row_of(l[i].key, row, sizeof row);
        if (!found) {
            printf("  LICENSE.md has no table row naming '%s' (%s)\n", l[i].key, l[i].name);
        }
        ASSERT_TRUE(found);
        if (found && strstr(row, l[i].token) == NULL) {
            printf("  the LICENSE.md row of '%s' does not mention '%s'\n", l[i].name, l[i].token);
            ASSERT_TRUE(false);
        }
    }
}

TEST(entries_are_complete_unique_and_short) {
    size_t n;
    const ui_licence_t *l = ui_licence_all(&n);
    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(l[i].name[0] != '\0' && l[i].licence[0] != '\0' && l[i].notice[0] != '\0' && l[i].key[0] != '\0' && l[i].token[0] != '\0');
        ASSERT_TRUE(strlen(l[i].notice) <= UI_LICENCE_NOTICE_MAX); // drawn without scrolling: about 13 lines of 40 characters at 320x240
        ASSERT_TRUE(strlen(l[i].name) <= 40 && strlen(l[i].licence) <= 24);
        for (size_t k = i + 1; k < n; k++) {
            ASSERT_TRUE(strcmp(l[i].name, l[k].name) != 0);
        }
    }
}

TEST(the_y2_does_not_list_the_rp2350_only_parts) {
    size_t all;
    ui_licence_all(&all);
    size_t rp = ui_licence_count("rp2350"), y2 = ui_licence_count("y2"), host = ui_licence_count("host");
    ASSERT_EQ(all, rp);
    ASSERT_EQ(all, host); // the simulator shows everything (the tour)
    ASSERT_TRUE(y2 < all && y2 >= 10);
    for (size_t i = 0; i < y2; i++) {
        ASSERT_FALSE(ui_licence_get("y2", i)->rp2350_only);
    }
    ASSERT_TRUE(ui_licence_get("y2", y2) == NULL);
    ASSERT_TRUE(ui_licence_get("y2", 0) != NULL);
    ASSERT_EQ(0, strcmp("neuPod firmware", ui_licence_get("y2", 0)->name));
    ASSERT_EQ(y2, ui_licence_count(NULL)); // no platform: nothing RP2350-only
}

TEST(btstack_says_non_commercial) {
    size_t n;
    const ui_licence_t *l = ui_licence_all(&n);
    bool seen = false;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(l[i].name, "BTstack") == 0) {
            seen = true;
            ASSERT_TRUE(strstr(l[i].licence, "Non-commercial") != NULL);
            ASSERT_TRUE(strstr(l[i].notice, "commercial") != NULL);
        }
    }
    ASSERT_TRUE(seen);
}

int main(void) {
    RUN(every_listed_component_is_in_licence_md_with_its_licence);
    RUN(entries_are_complete_unique_and_short);
    RUN(the_y2_does_not_list_the_rp2350_only_parts);
    RUN(btstack_says_non_commercial);
    return RESULT();
}
