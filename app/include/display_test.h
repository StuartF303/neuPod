// SPDX-License-Identifier: MPL-2.0
/**
 * @file display_test.h
 * @brief Portable display test pattern (colour bars + gradient), HAL-only.
 */
#ifndef NEUPOD_DISPLAY_TEST_H
#define NEUPOD_DISPLAY_TEST_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Draw 8 vertical colour bars over the top ~3/4 of the panel and a left-to-right grey
 * gradient strip underneath, using only hal_display. hal_display_init() must have been called.
 */
hal_status_t display_test_draw(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_DISPLAY_TEST_H
