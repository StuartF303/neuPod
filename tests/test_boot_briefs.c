// SPDX-License-Identifier: MPL-2.0
// Every boot-splash caption must fit on ONE line of the caption label (UI_W - 20 = 300 px, Montserrat 14)
// together with its "[n/m] name: " prefix, so nothing wraps or truncates into the version line.
#include <stdlib.h>
#include <string.h>

#include "boot_briefs.h"
#include "lvgl.h"
#include "src/misc/lv_text_private.h" /* lv_text_get_width: LVGL private API, fine for a host test */
#include <stdio.h>
#include "unit.h"

void neupod_panic_assert(const char *file, int line) {
    (void)file;
    (void)line;
    abort(); // LVGL's assert hook; not expected to fire
}

TEST(captions_fit_one_line) {
    static const char *const briefs[] = {BOOT_BRIEF_ALL};
    lv_init();
    lv_text_attributes_t attr = {0};
    const int32_t avail = 320 - 20;
    for (size_t i = 0; i < sizeof briefs / sizeof briefs[0]; i++) {
        ASSERT_TRUE(strlen(briefs[i]) <= 24); // documented limit
        char line[96];
        // Longest prefix: "[6/6] bluetooth: " (widest capability name, widest counter).
        snprintf(line, sizeof line, "[6/6] bluetooth: %s", briefs[i]);
        int32_t w = lv_text_get_width(line, (uint32_t)strlen(line), &lv_font_montserrat_14, &attr);
        if (w > avail) {
            printf("  caption too wide: %d px > %d: '%s'\n", (int)w, (int)avail, line);
        }
        ASSERT_TRUE(w <= avail);
    }
}

int main(void) {
    RUN(captions_fit_one_line);
    return RESULT();
}
