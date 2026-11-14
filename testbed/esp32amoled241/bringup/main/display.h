// SPDX-License-Identifier: MPL-2.0
// RM690B0 AMOLED (450x600 native) on QSPI, presented as 600x450 landscape from a PSRAM RGB565 framebuffer.
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool disp_init(void);              // panel + framebuffer; sets R_DISP. False when unusable.
void disp_test_pattern(void);      // colour bars, border, corner marks, banner text; pushes the frame
void disp_measure(void);           // sets R_FPS: full-frame push time and frame rate
void disp_brightness_test(void);   // sets R_BRIGHT: 50 % then 100 % through the panel's 0x51 command
void disp_set_brightness(uint8_t v);
void disp_cycle_mode(void);        // hardware landscape (MADCTL) -> software rotate A -> software rotate B
const char *disp_mode_name(void);
void disp_cycle_touch_map(void);   // cycle the 8 swap/mirror variants of the raw touch coordinates
void disp_map_touch(uint16_t rx, uint16_t ry, int *lx, int *ly);
const char *disp_touch_map_name(void);
void disp_dot(int lx, int ly, int radius);   // paint a white dot and push just that area
