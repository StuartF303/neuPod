// SPDX-License-Identifier: MPL-2.0
// THROWAWAY phase-0 measurement mode (docs/design/2026-11-21-ui-styles/IMPLEMENTATION-BRIEF.md): built only with -DNEUPOD_UI_BENCH=ON.
#ifndef NEUPOD_UI_BENCH_H
#define NEUPOD_UI_BENCH_H

#include <stdbool.h>
#include <stddef.h>

/** Runs every bench in the UI task, forever (a 2 s gap between rounds). Never returns. */
void ui_bench_main(void);

/** Big-buffer allocation with an explicit placement. Weak malloc default in ui_bench.c (host); the targets override it. Free in reverse order. */
void *ui_bench_alloc(size_t bytes, bool psram);
void ui_bench_free(void *p, bool psram);

#endif
