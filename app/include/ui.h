// SPDX-License-Identifier: MPL-2.0
/**
 * @file ui.h
 * @brief LVGL user interface task: boot splash, then the home screen (docs/runtime.md, ADR-0010).
 *
 * LVGL is not thread-safe and runs in this one task only (LV_USE_OS = NONE); every other task
 * talks to it through the capabilities struct, the battery store and the input queue.
 */
#ifndef NEUPOD_UI_H
#define NEUPOD_UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UI performance counters for the monitor line (see ui_internal.h for the fields). */
typedef struct {
    uint32_t frames;       /**< passes that rendered something since the last window reset */
    uint32_t frame_max_us; /**< longest LVGL pass (render + blocking flush) in the window */
    uint32_t frame_avg_us;
    uint32_t mem_used;     /**< LVGL pool bytes in use / total / high-water since boot */
    uint32_t mem_total;
    uint32_t mem_max_used;
    uint32_t mem_frag_pct;
} ui_perf_t;
bool ui_perf_get(ui_perf_t *out);

/**
 * True while Hold is on (docs/adr/0020-hold.md). Safe to call from any task (the Y2's side volume keys, read on the input task, are locked with the
 * rest of the hardware keys; headset keys are not).
 */
bool ui_locked(void);
void ui_perf_new_window(void);

/** Task body for the UI task (display init -> splash -> boot checks -> home -> event loop). */
void ui_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_UI_H
