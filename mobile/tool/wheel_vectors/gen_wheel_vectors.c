// SPDX-License-Identifier: MPL-2.0
/*
 * Test vectors for the app's ports of the firmware's wheel logic, made by the firmware's own code:
 * ui_accel_feed() from app/ui/ui_logic.c and wi_letterjump_scroll() / wi_letterjump_tick() from
 * app/ui/input/wheel_input.c, compiled for the host. The functions are pulled from the firmware sources
 * by gen_wheel_vectors.sh (not copied here), so the vectors always come from the current code.
 *
 * Output: JSON on stdout, read by mobile/test/domain/wheel/firmware_vectors_test.dart.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime_config.h"
#include "wheel_input.h"

typedef struct {
    int32_t residue;
    uint32_t energy;
    uint32_t last_ms;
} ui_accel_t;

static void ui_accel_init(ui_accel_t *a) {
    a->residue = 0;
    a->energy = 0;
    a->last_ms = 0;
}

#include "extracted.inc" /* ui_accel_feed, norm, wi_letterjump_init, wi_letterjump_scroll, wi_letterjump_tick */

static uint32_t g_rs = 12345u;
static uint32_t rnd(void) {
    g_rs = g_rs * 1103515245u + 12345u;
    return g_rs >> 8;
}

int main(void) {
    printf("{\"accel\":[");
    for (int seq = 0; seq < 60; seq++) {
        ui_accel_t a;
        ui_accel_init(&a);
        uint32_t t = seq == 59 ? 0xFFFFFF00u : seq == 58 ? 0u : rnd() % 100000u; /* 59: across the 2^32 wrap */
        printf("%s[", seq ? "," : "");
        int n = 40 + (int)(rnd() % 60);
        for (int i = 0; i < n; i++) {
            uint32_t mode = rnd() % 10;
            int32_t c;
            if (mode < 5) {
                c = (int32_t)(rnd() % 5) + 1;
            } else if (mode < 7) {
                c = -(int32_t)(rnd() % 5) - 1;
            } else if (mode < 9) {
                c = (int32_t)(rnd() % 3);
            } else {
                c = (int32_t)(rnd() % 60) - 30;
            }
            uint32_t gap = (rnd() % 4 == 0) ? rnd() % 400 : rnd() % 40;
            if (seq == 57 && i == 20) {
                gap = 0xFFFFFFF0u; /* the clock goes back 16 ms */
            }
            t += gap;
            int32_t s = ui_accel_feed(&a, c, t);
            printf("%s[%d,%u,%d,%d,%u]", i ? "," : "", c, t, s, a.residue, a.energy);
        }
        printf("]");
    }
    printf("],\"letterjump\":[");
    for (int seq = 0; seq < 30; seq++) {
        wi_letterjump_t j;
        wi_letterjump_init(&j);
        uint32_t t = seq == 29 ? 0xFFFFFF00u : rnd() % 100000u;
        printf("%s[", seq ? "," : "");
        int n = 30 + (int)(rnd() % 40);
        for (int i = 0; i < n; i++) {
            static const char ini[] = "aZm7#bQ x";
            char c = ini[rnd() % 9];
            t += (rnd() % 3 == 0) ? rnd() % 600 : rnd() % 120;
            if (rnd() % 3 == 0) {
                wi_letterjump_tick(&j, t);
                printf("%s[\"tick\",%u,%d,%u]", i ? "," : "", t, j.active, j.energy);
            } else {
                int32_t s = (int32_t)(rnd() % 9) - 4;
                if (rnd() % 4 == 0) {
                    s *= 8;
                }
                bool r = wi_letterjump_scroll(&j, s, t, c);
                printf("%s[\"scroll\",%u,%d,\"%c\",%d,%u,\"%c\"]", i ? "," : "", t, s, c, r, j.energy, j.letter);
            }
        }
        printf("]");
    }
    printf("]}\n");
    return 0;
}
