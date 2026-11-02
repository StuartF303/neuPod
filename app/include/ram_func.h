// SPDX-License-Identifier: MPL-2.0
// NEUPOD_RAM_FUNC(name): place a function in RAM on the RP2350 (same as the SDK's __not_in_flash_func)
// so the bare-metal audio core does not fight the UI for the XIP flash cache. Empty elsewhere.
//   static void NEUPOD_RAM_FUNC(on_block)(uint32_t missed) { ... }
#ifndef NEUPOD_RAM_FUNC_H
#define NEUPOD_RAM_FUNC_H

#if defined(NEUPOD_RP2350) && NEUPOD_RP2350 /* defined by the top-level CMakeLists for the RP2350 build */
#define NEUPOD_RAM_FUNC(name) __attribute__((section(".time_critical." #name), noinline)) name
#else
#define NEUPOD_RAM_FUNC(name) name
#endif

#endif
