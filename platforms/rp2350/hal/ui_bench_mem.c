// SPDX-License-Identifier: MPL-2.0
// THROWAWAY (phase-0 bench): big buffers for ui_bench.c. PSRAM = the board's 8 MB on QMI CS1 (bench_psram.c, the bring-up probe's driver), a
// bump allocator that frees in stack order only (the bench frees in reverse). "Internal" = os_alloc (FreeRTOS heap, SRAM).
#include <stdint.h>
#include <stdio.h>

#include "bench_psram.h"
#include "os.h"

#define PSRAM_BASE 0x11000000u /* XIP window 1, cached */
#define PSRAM_SIZE (8u * 1024u * 1024u)

static bool s_init, s_ok;
static uint32_t s_top;

typedef struct {
    uint32_t size, off;
    uint32_t pad[2]; /* keep 16-byte alignment */
} hdr_t;

void *ui_bench_alloc(size_t bytes, bool psram) {
    if (!psram) {
        return os_alloc(bytes);
    }
    if (!s_init) {
        s_init = true;
        psram_info_t pi;
        psram_init(47, &pi);
        s_ok = pi.present;
        printf("bench: psram present=%d size=%u MB sck=%u Hz\n", (int)pi.present, (unsigned)(pi.size >> 20), (unsigned)pi.sck_hz);
    }
    if (!s_ok) {
        return NULL;
    }
    uint32_t need = ((uint32_t)bytes + sizeof(hdr_t) + 15u) & ~15u;
    if (s_top + need > PSRAM_SIZE) {
        return NULL;
    }
    hdr_t *h = (hdr_t *)(PSRAM_BASE + s_top);
    h->size = need;
    h->off = s_top;
    s_top += need;
    return h + 1;
}

void ui_bench_free(void *p, bool psram) {
    if (p == NULL) {
        return;
    }
    if (!psram) {
        os_free(p);
        return;
    }
    hdr_t *h = (hdr_t *)p - 1;
    if (h->off + h->size == s_top) {
        s_top = h->off;
    } /* else: not the last block, leaked until the bench restarts (it frees in reverse) */
}
