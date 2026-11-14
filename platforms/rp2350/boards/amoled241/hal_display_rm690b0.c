// SPDX-License-Identifier: MPL-2.0
/*
 * RM690B0 AMOLED driver (Waveshare RP2350-Touch-AMOLED-2.41): 450x600 native, used as 600x450 landscape, RGB565.
 * Our own code, written from the controller's command set and the facts the bench probe (testbed/amoled241/bringup) established;
 * no vendor driver code is used.
 *
 * Bus: QSPI by one PIO state machine (rm690b0_qspi.pio) plus one DMA channel, chip select as a plain GPIO.
 *   - A register write is: CS low, 0x02, 0x00, <register>, 0x00, the parameters, CS high. All of it on D0 only (the PIO program
 *     sends nibbles, so the driver expands each bit into a nibble).
 *   - Pixels are: CS low, 0x32, 0x00, 0x2C, 0x00 on D0, then the RGB565 stream on D0..D3 (high byte first), CS high. After the
 *     first blit CS stays low across successive hal_display_blit() calls (they continue filling the window, like the other HALs)
 *     until the next register write ends the stream.
 *
 * Orientation: MADCTL (0x36) 0x60 = MX|MV makes the controller itself address the panel as landscape, so the stream the UI
 * produces (row-major, left to right) goes straight to the glass. Its column/page start offsets are 0 and 14 (the 450 visible
 * columns sit 16 from one end of the controller's 480-column memory and 14 from the other; which end is "first" depends on the
 * mirror bit). NEUPOD_AMOLED_LANDSCAPE=3 selects the other landscape (0xA0, offset 16), turned by 180 degrees.
 *
 * The controller wants window starts and sizes in whole pairs of pixels: set_window() refuses odd values with HAL_EINVAL (the UI
 * rounds its flush areas outwards before calling).
 */
#include <stdio.h>

#include "board.h"
#include "hal_display.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "rm690b0_qspi.pio.h"

#if NEUPOD_AMOLED_LANDSCAPE == 3
#define AMOLED_MADCTL_LANDSCAPE 0xA0u // MY|MV
#define AMOLED_PAGE_OFFSET 16u
#else
#define AMOLED_MADCTL_LANDSCAPE 0x60u // MX|MV
#define AMOLED_PAGE_OFFSET 14u
#endif
#define AMOLED_COLUMN_OFFSET 0u

// Controller commands (standard MIPI DCS numbers plus the vendor ones the init sequence needs).
#define CMD_SLPIN 0x10
#define CMD_SLPOUT 0x11
#define CMD_INVOFF 0x20
#define CMD_INVON 0x21
#define CMD_DISPOFF 0x28
#define CMD_DISPON 0x29
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A
#define CMD_TEON 0x35
#define CMD_BRIGHTNESS 0x51

#define PIO_IDLE_TIMEOUT_US 100000u

static PIO g_pio;
static uint g_sm;
static int g_dma = -1;
static bool g_ready;
static bool g_stream_open; // CS is low and a pixel stream is open
static bool g_dma_active;
static bool g_sleeping;
static uint8_t g_madctl = AMOLED_MADCTL_LANDSCAPE;
static bool g_inversion;
static uint8_t g_brightness;
static uint64_t g_slpin_us;
static hal_pixel_t g_fill_px; // DMA source of fill_rect; must outlive the transfer

static inline void cs_low(void) { gpio_put(AMOLED_PIN_CS, 0); }
static inline void cs_high(void) { gpio_put(AMOLED_PIN_CS, 1); }

// Everything the CPU or DMA queued has left the pins: the state machine has run dry (TXSTALL: it stalled on an empty FIFO in the
// autopull, which is the only place it can stall). The flag is sticky, so clear it first; it sets again at once if the machine is
// already idle. Bounded, so a dead PIO cannot hang the caller.
static void pio_wait_idle(void) {
    const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + g_sm);
    uint64_t t0 = time_us_64();
    // First until the FIFO is empty: the last word is then in the OSR and being shifted out, so a stall seen after clearing the flag
    // is the one that follows it (clearing earlier could meet the stall of the idle machine that has not yet noticed a new word).
    while (!pio_sm_is_tx_fifo_empty(g_pio, g_sm)) {
        if (time_us_64() - t0 > PIO_IDLE_TIMEOUT_US) {
            return;
        }
        tight_loop_contents();
    }
    g_pio->fdebug = stall;
    while (!(g_pio->fdebug & stall)) {
        if (time_us_64() - t0 > PIO_IDLE_TIMEOUT_US) {
            break;
        }
        tight_loop_contents();
    }
}

// One byte on D0 only, MSB first: eight nibbles, packed four to a 16-bit autopull word (two words per byte).
static inline void put_1wire(uint8_t b) {
    uint32_t hi = ((uint32_t)((b >> 7) & 1u) << 12) | ((uint32_t)((b >> 6) & 1u) << 8) | ((uint32_t)((b >> 5) & 1u) << 4) |
                  (uint32_t)((b >> 4) & 1u);
    uint32_t lo = ((uint32_t)((b >> 3) & 1u) << 12) | ((uint32_t)((b >> 2) & 1u) << 8) | ((uint32_t)((b >> 1) & 1u) << 4) |
                  (uint32_t)(b & 1u);
    pio_sm_put_blocking(g_pio, g_sm, hi << 16); // the OSR shifts out the top 16 bits of the word
    pio_sm_put_blocking(g_pio, g_sm, lo << 16);
}

void hal_display_wait(void) {
    if (g_dma_active) {
        dma_channel_wait_for_finish_blocking((uint)g_dma);
        g_dma_active = false;
    }
    if (g_ready) {
        pio_wait_idle();
    }
}

// Any register write first closes an open pixel stream.
static void write_reg(uint8_t reg, const uint8_t *data, size_t len) {
    hal_display_wait();
    cs_high();
    g_stream_open = false;
    busy_wait_us(1); // CS high time between transfers
    cs_low();
    put_1wire(0x02);
    put_1wire(0x00);
    put_1wire(reg);
    put_1wire(0x00);
    for (size_t i = 0; i < len; i++) {
        put_1wire(data[i]);
    }
    pio_wait_idle();
    cs_high();
}

static void write_reg1(uint8_t reg, uint8_t value) { write_reg(reg, &value, 1); }

static void write_range(uint8_t cmd, uint16_t start, uint16_t end) {
    uint8_t d[4] = {(uint8_t)(start >> 8), (uint8_t)start, (uint8_t)(end >> 8), (uint8_t)end};
    write_reg(cmd, d, sizeof d);
}

static hal_status_t hw_init_once(void) {
    if (g_ready) {
        return HAL_OK;
    }
    gpio_init(AMOLED_PIN_CS);
    gpio_put(AMOLED_PIN_CS, 1);
    gpio_set_dir(AMOLED_PIN_CS, GPIO_OUT);
    gpio_init(AMOLED_PIN_RST);
    gpio_put(AMOLED_PIN_RST, 1);
    gpio_set_dir(AMOLED_PIN_RST, GPIO_OUT);
    gpio_init(AMOLED_PIN_PWR_EN);
    gpio_put(AMOLED_PIN_PWR_EN, 1); // panel supply on
    gpio_set_dir(AMOLED_PIN_PWR_EN, GPIO_OUT);

    uint offset;
    if (!pio_claim_free_sm_and_add_program(&rm690b0_qspi_program, &g_pio, &g_sm, &offset)) {
        return HAL_EBUSY;
    }
    g_dma = dma_claim_unused_channel(false);
    if (g_dma < 0) {
        pio_remove_program_and_unclaim_sm(&rm690b0_qspi_program, g_pio, g_sm, offset);
        return HAL_EBUSY;
    }
    pio_gpio_init(g_pio, AMOLED_PIN_CLK);
    for (uint i = 0; i < 4; i++) {
        pio_gpio_init(g_pio, AMOLED_PIN_D0 + i);
    }
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, AMOLED_PIN_CLK, 1, true);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, AMOLED_PIN_D0, 4, true);
    pio_sm_config c = rm690b0_qspi_make_config(offset, AMOLED_PIN_CLK, AMOLED_PIN_D0);
    pio_sm_init(g_pio, g_sm, offset, &c);
    pio_sm_set_enabled(g_pio, g_sm, true);

    dma_channel_config dc = dma_channel_get_default_config((uint)g_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(g_pio, g_sm, true));
    dma_channel_set_config((uint)g_dma, &dc, false);
    dma_channel_set_write_addr((uint)g_dma, &g_pio->txf[g_sm], false);
    g_ready = true;
    return HAL_OK;
}

hal_status_t hal_display_init(void) {
    hal_status_t st = hw_init_once();
    if (st != HAL_OK) {
        return st;
    }
    g_dma_active = false;
    g_stream_open = false;
    g_sleeping = false;
    g_madctl = AMOLED_MADCTL_LANDSCAPE;
    g_inversion = false;
    g_brightness = 0;

    // Hardware reset (timings of the vendor sequence the probe ran on this board).
    gpio_put(AMOLED_PIN_RST, 1);
    sleep_ms(50);
    gpio_put(AMOLED_PIN_RST, 0);
    sleep_ms(50);
    gpio_put(AMOLED_PIN_RST, 1);
    sleep_ms(300);

    write_reg(CMD_SLPOUT, NULL, 0);
    sleep_ms(120);
    write_reg(0x44, (const uint8_t[]){0x01, 0xD1}, 2); // TE scan line
    write_reg1(0xFE, 0x20);                            // switch to the controller's user-command page 2 ...
    write_reg1(0x63, 0xFF);
    write_reg1(0x26, 0x0A);
    write_reg1(0x24, 0x80);                            // ... (display RAM written over the SPI interface)
    write_reg1(0xFE, 0x00);                            // ... and back to page 0
    write_reg1(CMD_COLMOD, 0x55);                      // 16 bits per pixel
    write_reg1(0xC4, 0x80);                            // quad-wire pixel interface
    write_reg1(0xC2, 0x00);
    sleep_ms(10);
    write_reg1(CMD_TEON, 0x00);
    write_reg1(CMD_MADCTL, g_madctl);                  // landscape
    write_reg1(CMD_BRIGHTNESS, 0x00);                  // dark until the UI asks for light (hal_display_backlight)
    write_reg(CMD_DISPON, NULL, 0);
    sleep_ms(10);

    // Frame memory powers up with noise: clear it to black (brightness is 0 meanwhile).
    st = hal_display_fill_rect(0, 0, AMOLED_WIDTH, AMOLED_HEIGHT, 0);
    if (st != HAL_OK) {
        return st;
    }
    printf("display: RM690B0 %ux%u landscape (MADCTL 0x%02X), QSPI PIO clk_sys %u Hz -> SCLK %u Hz\n", (unsigned)AMOLED_WIDTH,
           (unsigned)AMOLED_HEIGHT, (unsigned)g_madctl, (unsigned)clock_get_hz(clk_sys), (unsigned)(clock_get_hz(clk_sys) / 2u));
    return HAL_OK;
}

const char *hal_display_name(void) { return "RM690B0"; }
uint16_t hal_display_width(void) { return AMOLED_WIDTH; }
uint16_t hal_display_height(void) { return AMOLED_HEIGHT; }

hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_ready) {
        return HAL_ENODEV;
    }
    if (w == 0 || h == 0 || (uint32_t)x + w > AMOLED_WIDTH || (uint32_t)y + h > AMOLED_HEIGHT) {
        return HAL_EINVAL;
    }
    if (((x | y | w | h) & 1u) != 0) { // the controller addresses whole pixel pairs
        return HAL_EINVAL;
    }
    write_range(CMD_CASET, (uint16_t)(x + AMOLED_COLUMN_OFFSET), (uint16_t)(x + w - 1u + AMOLED_COLUMN_OFFSET));
    write_range(CMD_RASET, (uint16_t)(y + AMOLED_PAGE_OFFSET), (uint16_t)(y + h - 1u + AMOLED_PAGE_OFFSET));
    write_reg(CMD_RAMWR, NULL, 0);
    return HAL_OK;
}

// Open the pixel stream if it is not open: CS low, quad pixel write to RAMWR. CS then stays low until the next register write.
static void begin_pixel_stream(void) {
    hal_display_wait();
    if (!g_stream_open) {
        cs_low();
        put_1wire(0x32);
        put_1wire(0x00);
        put_1wire(CMD_RAMWR);
        put_1wire(0x00);
        g_stream_open = true;
    }
}

static void start_dma(const hal_pixel_t *src, size_t count, bool increment) {
    dma_channel_config dc = dma_get_channel_config((uint)g_dma);
    channel_config_set_read_increment(&dc, increment);
    dma_channel_set_config((uint)g_dma, &dc, false);
    g_dma_active = true;
    dma_channel_transfer_from_buffer_now((uint)g_dma, src, (uint32_t)count);
}

hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count) {
    if (!g_ready) {
        return HAL_ENODEV;
    }
    if (pixels == NULL) {
        return HAL_EINVAL;
    }
    if (count == 0) {
        return HAL_OK;
    }
    begin_pixel_stream();
    start_dma(pixels, count, true);
    return HAL_OK;
}

hal_status_t hal_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal_pixel_t colour) {
    hal_status_t st = hal_display_set_window(x, y, w, h); // validates (fit and even alignment)
    if (st != HAL_OK) {
        return st;
    }
    g_fill_px = colour;
    begin_pixel_stream();
    start_dma(&g_fill_px, (size_t)w * h, false); // one pixel read over and over: no buffer needed
    hal_display_wait();
    return HAL_OK;
}

void hal_display_present(void) {
    // The controller has its own frame memory; nothing to flip.
}

void hal_display_service(void) {}

void hal_display_backlight(uint8_t level) {
    g_brightness = level;
    if (g_ready && !g_sleeping) {
        write_reg1(CMD_BRIGHTNESS, level);
    }
}

void hal_display_sleep(bool sleep) {
    if (!g_ready || sleep == g_sleeping) {
        return;
    }
    if (sleep) {
        write_reg(CMD_DISPOFF, NULL, 0); // blank first so the panel does not show the power-down transient
        write_reg(CMD_SLPIN, NULL, 0);
        g_slpin_us = time_us_64();
        sleep_ms(5);
    } else {
        uint64_t since = time_us_64() - g_slpin_us; // >= 120 ms between SLPIN and SLPOUT
        if (since < 120000u) {
            sleep_us((uint32_t)(120000u - since));
        }
        write_reg(CMD_SLPOUT, NULL, 0);
        sleep_ms(120);
        write_reg(CMD_DISPON, NULL, 0);
        g_sleeping = false;
        write_reg1(CMD_BRIGHTNESS, g_brightness); // the register may have been reset by the sleep
    }
    g_sleeping = sleep;
}

void hal_display_set_madctl(uint8_t madctl) {
    g_madctl = madctl;
    if (g_ready) {
        write_reg1(CMD_MADCTL, madctl);
    }
}

uint8_t hal_display_get_madctl(void) { return g_madctl; }

void hal_display_set_inversion(bool on) {
    g_inversion = on;
    if (g_ready) {
        write_reg(on ? CMD_INVON : CMD_INVOFF, NULL, 0);
    }
}

bool hal_display_get_inversion(void) { return g_inversion; }
