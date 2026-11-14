// SPDX-License-Identifier: MPL-2.0
// Boards without a touch panel (the Pico 2 W, the Y2): the input task sees hal_touch_present() == false and never polls.
#include "hal_touch.h"

bool hal_touch_present(void) { return false; }

hal_status_t hal_touch_init(void) { return HAL_ENOTSUP; }

bool hal_touch_read(hal_touch_point_t *out) {
    if (out != NULL) {
        *out = (hal_touch_point_t){0, 0, false};
    }
    return false;
}
