// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_console.h
 * @brief Console input. Output is plain stdio (printf/puts); this adds the missing keypress read.
 *
 * RP2350: UART0 (GP0/GP1, 115200 8N1), the same stdio console as printf. Host: the terminal's
 * stdin in raw (unbuffered, no echo) mode, restored on exit.
 */
#ifndef NEUPOD_HAL_CONSOLE_H
#define NEUPOD_HAL_CONSOLE_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wait up to @p ms milliseconds for one input byte. Returns the byte (0..255), or -1 on
 * timeout. @p ms == 0 polls without waiting. On the host, stdin end-of-file exits the process
 * (there is nobody left to type), and the simulator window keeps being serviced while waiting.
 */
int hal_console_getc_timeout(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_CONSOLE_H
