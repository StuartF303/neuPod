// SPDX-License-Identifier: MPL-2.0
// Private to platforms/rp2350/hal: the virtual block clock (hal_audio_vclk.c). Core-1 rules apply to everything behind it (ADR-0011).
#ifndef NEUPOD_VCLK_H
#define NEUPOD_VCLK_H

#include "hal_audio.h"

void vclk_set_callback(hal_audio_block_cb cb);
/** Arm the block alarm on the calling core at @p rate; the first block comes one period later. */
hal_status_t vclk_open(uint32_t rate);
void vclk_close(void);
uint32_t vclk_underruns(void);

#endif // NEUPOD_VCLK_H
