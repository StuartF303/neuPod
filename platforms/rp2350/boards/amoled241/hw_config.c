// SPDX-License-Identifier: MPL-2.0
/*
 * microSD configuration for carlk3's no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library (Apache-2.0), SPI mode, for the Waveshare
 * RP2350-Touch-AMOLED-2.41: the TF slot is wired to SPI0 (SCK GP18, MOSI GP19, MISO GP20, CS GP23; the board also routes the SDIO
 * D1/D2 lines to GP21/GP22, which SPI mode does not use). There is no card-detect switch.
 *
 * Only SDHC/SDXC cards work in this library's SPI mode (it refuses standard-capacity cards).
 */
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hw_config.h"
#include "sd_card.h"

// Data clock. The PL022 divides clk_peri (125 MHz) by an even number: 125 / 6 = 20.8 MHz, inside the 25 MHz of default-speed
// cards. The library switches to 100-400 kHz itself for identification.
#define SD_SPI_BAUD_HZ 20000000u

static spi_t g_spi = {
    .hw_inst = spi0,
    .sck_gpio = 18,
    .mosi_gpio = 19,
    .miso_gpio = 20,
    .baud_rate = SD_SPI_BAUD_HZ,
};

static sd_spi_if_t g_spi_if = {
    .spi = &g_spi,
    .ss_gpio = 23,
};

static sd_card_t g_sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &g_spi_if,
    .use_card_detect = false,
};

size_t sd_get_num(void) { return 1; }

sd_card_t *sd_get_by_num(size_t num) { return num == 0 ? &g_sd_card : NULL; }
