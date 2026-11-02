// SPDX-License-Identifier: MPL-2.0
// LVGL assertion failures take the same fatal path as FreeRTOS asserts (app/runtime/panic.c).
#ifndef NEUPOD_LV_ASSERT_HANDLER_H
#define NEUPOD_LV_ASSERT_HANDLER_H

#include "panic.h"

#define LV_ASSERT_HANDLER neupod_panic_assert(__FILE__, __LINE__);

#endif
