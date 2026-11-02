// SPDX-License-Identifier: MPL-2.0
/*
 * microSD configuration for carlk3's no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library (Apache-2.0).
 *
 * Pins (Phase 1 testbed, confirmed): SDIO 4-bit, 3.3 V.
 *   CMD = GP3, DAT0..DAT3 = GP4..GP7 (consecutive), CLK = GP2.
 * The library derives CLK = (D0 - 2) mod 32 and D1..D3 = D0 + 1..3, so only D0 and CMD are given
 * here; GP2 = GP4 - 2 matches.
 *
 * PIO / DMA allocation (see also docs): the SD library takes pio1, whole (it clears the
 * instruction memory of the PIO it is given, and it only supports pio0/pio1 for the pin function,
 * so pio2 is not an option). It claims two state machines on pio1 and two DMA channels
 * dynamically (dma_claim_unused_channel) and shares DMA_IRQ_0. The CYW43 driver and the click-wheel
 * receiver pick PIOs with pio_claim_free_sm_and_add_program*, which searches from the highest
 * instance down, so they land on pio2 and never touch pio1. hal_storage_init() refuses to start
 * (HAL_STORAGE_BUSY) if some other code has already claimed a state machine on pio1.
 */
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hw_config.h"
#include "sd_card.h"
#include "sdio_config.h"

#define SDIO_PIN_CMD 3
#define SDIO_PIN_D0 4

static sd_sdio_if_t g_sdio_if = {
    .CMD_gpio = SDIO_PIN_CMD,
    .D0_gpio = SDIO_PIN_D0,
    .SDIO_PIO = pio1,
    .DMA_IRQ_num = DMA_IRQ_0,
    .use_exclusive_DMA_IRQ_handler = false, // share the IRQ; nothing else here uses DMA IRQs
    .baud_rate = NEUPOD_SDIO_BAUD_HZ,
};

static sd_card_t g_sd_card = {
    .type = SD_IF_SDIO,
    .sdio_if_p = &g_sdio_if,
    .use_card_detect = false, // the breakout's card-detect switch is not wired
};

size_t sd_get_num(void) { return 1; }

sd_card_t *sd_get_by_num(size_t num) { return num == 0 ? &g_sd_card : NULL; }
