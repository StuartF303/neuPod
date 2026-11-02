// SPDX-License-Identifier: MPL-2.0
#include "hal_console.h"
#include "pico/stdlib.h"

int hal_console_getc_timeout(uint32_t ms) {
    // getchar_timeout_us takes a 32-bit microsecond count; clamp to ~71 minutes.
    uint32_t us = ms > 4000000u ? 4000000000u : ms * 1000u;
    int c = getchar_timeout_us(us);
    return c < 0 ? -1 : (c & 0xFF); // PICO_ERROR_TIMEOUT is negative
}
