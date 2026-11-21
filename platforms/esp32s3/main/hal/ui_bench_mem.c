// SPDX-License-Identifier: MPL-2.0
// THROWAWAY (phase-0 bench): big buffers for ui_bench.c with an explicit placement: PSRAM or internal RAM.
#include <stdbool.h>
#include <stddef.h>

#include "esp_heap_caps.h"

void *ui_bench_alloc(size_t bytes, bool psram) {
    return heap_caps_malloc(bytes, (psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) | MALLOC_CAP_8BIT);
}

void ui_bench_free(void *p, bool psram) {
    (void)psram;
    heap_caps_free(p);
}
