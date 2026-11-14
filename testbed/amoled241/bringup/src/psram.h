// SPDX-License-Identifier: MPL-2.0
// Own PSRAM bring-up for an APS6404L-class quad PSRAM on QMI chip-select 1 (RP2350). Written from the RP2350 datasheet (QMI chapter)
// and the APS6404L datasheet; not derived from the Waveshare/4D Systems psram_tool.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "pico/types.h"

typedef struct {
    bool present;       // KGD read back as 0x5D
    uint8_t mf, kgd, eid;
    uint32_t size;      // bytes, from the EID density field (the caller should still check by address aliasing)
    uint32_t clkdiv;    // QMI M1 clock divider chosen
    uint32_t sck_hz;    // resulting PSRAM clock
    uint32_t rxdelay, max_select, min_deselect;  // the timing fields written
} psram_info_t;

// Initialises the chip on @p cs_pin (QMI CS1; GPIO47 on this board) for the current clk_sys and maps it at 0x11000000 (cached) and
// 0x15000000 (uncached), writable. Call after the final clk_sys is set; call again after changing it.
// Interrupts are off for a few hundred microseconds while the QMI is in direct mode.
void psram_init(uint cs_pin, psram_info_t *out);
