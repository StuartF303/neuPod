// SPDX-License-Identifier: MPL-2.0
/**
 * @file psram_bss.h
 * @brief NEUPOD_PSRAM_BSS: put a big, cold static buffer in PSRAM where the build has a PSRAM section (UI-25,
 *        specs/001-amoled-full-art research R4).
 *
 * Only the RP2350 AMOLED build defines NEUPOD_PSRAM_SECTION. There the Pico SDK links `.psram_uninitialised.*` into its PSRAM
 * region (NOLOAD), and platforms/rp2350/boards/amoled241/psram_amoled.c zeroes that range before the runtime starts, so the
 * statics still start at zero as C promises; the runtime refuses to start when the PSRAM check failed (runtime.c).
 * Everywhere else the macro is empty: the host, the Y2 and the Pico 2 W keep these buffers in RAM, and the ESP32-S3 moves whole
 * objects to PSRAM in platforms/esp32s3/main/linker.lf instead.
 *
 * Only for zero-initialised statics: an initialiser on a NEUPOD_PSRAM_BSS variable is lost (the section is NOLOAD and is zeroed, so it
 * starts at zero on the RP2350 AMOLED board and with its initialiser everywhere else).
 */
#ifndef NEUPOD_PSRAM_BSS_H
#define NEUPOD_PSRAM_BSS_H

#ifdef NEUPOD_PSRAM_SECTION
#define NEUPOD_PSRAM_BSS __attribute__((section(".psram_uninitialised.neupod")))
#else
#define NEUPOD_PSRAM_BSS
#endif

#endif // NEUPOD_PSRAM_BSS_H
