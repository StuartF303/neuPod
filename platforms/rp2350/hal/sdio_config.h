// SPDX-License-Identifier: MPL-2.0
// Private SDIO constants shared by hw_config.c and hal_storage.c.
#ifndef NEUPOD_SDIO_CONFIG_H
#define NEUPOD_SDIO_CONFIG_H

/*
 * Default data-phase SDIO clock. The library's PIO program needs 4 PIO cycles per SD clock, so
 *   sdio_clk = clk_sys / (4 * clk_div)   ->   125 MHz / (4 * 2) = 15.625 MHz
 * Integer divider chosen on purpose. Bench 2026-11-02 (Phase 1 breadboard, SanDisk SU64G):
 * command-response CRC errors were seen at fractional dividers (12.5 MHz = 2.5, 20.8 = 1.5,
 * 25 = 1.25) and once at 15.625 during the CMD6 sequence. They are deterministic (same response,
 * always the last CRC7 bit; see docs/storage.md) and not yet root-caused. Data tests 4-7 passed
 * with no errors at 15.625 MHz, so that is the default: in default-speed spec, no CMD6.
 * Integer steps at 125 MHz clk_sys: 31.25 (HS only), 15.625, 10.42, 7.81 MHz.
 */
#define NEUPOD_SDIO_BAUD_HZ 15625000u

// SD "default speed" maximum. Faster needs High Speed mode (CMD6) on the card.
#define NEUPOD_SDIO_DEFAULT_SPEED_MAX_HZ 25000000u

#endif // NEUPOD_SDIO_CONFIG_H
