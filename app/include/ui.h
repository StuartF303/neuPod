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

#ifdef __cplusplus
extern "C" {
#endif

/** Task body for the UI task (display init -> splash -> boot checks -> home -> event loop). */
void ui_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_UI_H
