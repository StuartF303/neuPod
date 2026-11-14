// SPDX-License-Identifier: MPL-2.0
/** @file hal_touch.h @brief Single-point touch panel. Coordinates are in the display's current orientation (0..hal_display_width()-1, 0..hal_display_height()-1). */
#ifndef NEUPOD_HAL_TOUCH_H
#define NEUPOD_HAL_TOUCH_H
#include "hal_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t x, y; bool down; } hal_touch_point_t;
/** Does this board have a touch panel? */
bool hal_touch_present(void);
/** Reset and configure the controller. HAL_ENOTSUP (or whatever "not supported" status hal_types.h has) where there is none. */
hal_status_t hal_touch_init(void);
/** Latest contact; input task only. Returns false if the read failed (out->down is then false). */
bool hal_touch_read(hal_touch_point_t *out);
#ifdef __cplusplus
}
#endif
#endif
