// SPDX-License-Identifier: MPL-2.0
/*
 * ST7789 display driver: SPI1 + DMA, 320x240 landscape.
 *
 * Pixel byte order: the ST7789 wants each RGB565 pixel MSB first. The PL022 SPI is run in
 * 16-bit frame mode for pixel data, which shifts the high byte out first, so native uint16_t
 * pixels from hal_pixel_t go out correctly with no byte swapping and DMA moves one halfword
 * per pixel. Commands and their parameters use 8-bit frame mode.
 *
 * Chip select is driven as a plain GPIO (not the SPI peripheral's CSn) so a multi-byte
 * command or a pixel stream stays inside one CS assertion. After RAMWR, CS stays low across
 * successive hal_display_blit() calls (they continue filling the window like the host
 * emulation does) until the next command (set_window) deasserts it. SPI1 has only this
 * device on it, so holding CS low in between is harmless.
 */
#include <stdio.h>

#include "hal_display.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

// --- Pin plan (Phase 1 testbed, confirmed) -------------------------------------------------
#define TFT_SPI spi1
#define TFT_PIN_MISO 12
#define TFT_PIN_CS 13
#define TFT_PIN_SCK 14
#define TFT_PIN_MOSI 15
#define TFT_PIN_DC 16
#define TFT_PIN_RST 17
#define TFT_PIN_SD_CS 18 // TFT breakout's SD card CS; unused, held high so it never selects
#define TFT_PIN_BACKLIGHT 8

// --- Panel geometry -------------------------------------------------------------------------
#define TFT_WIDTH 320u
#define TFT_HEIGHT 240u

// SPI clock. Deliberately conservative: 20 MHz is a full frame in ~62 ms and sits well inside
// what ST7789 parts accept, leaving margin for jumper-wire wiring on the testbed.
// TODO(hw): raise (30-60 MHz) once signal quality on the final wiring has been checked.
#define TFT_SPI_BAUD_HZ (20u * 1000u * 1000u)

// TODO(hw): UNCONFIRMED. MADCTL (0x36) value for this panel's landscape orientation.
// 0x60 = MX|MV is a placeholder; if the image is mirrored/flipped try 0xA0 (MY|MV) etc. Bit 3
// (0x08) selects BGR colour order: if red and blue are swapped, toggle it.
#define TFT_MADCTL_LANDSCAPE 0x60

// TODO(hw): UNCONFIRMED. Controller RAM offsets for this panel (some ST7789 modules have
// non-zero column/row offsets). 0 assumed for a native 320x240 module; verify on the panel.
#define TFT_COL_OFFSET 0
#define TFT_ROW_OFFSET 0

// TODO(hw): UNCONFIRMED. Backlight polarity: assumed active-high (PWM duty = brightness).
#define TFT_BACKLIGHT_ACTIVE_HIGH 1
// Tunable: backlight PWM frequency (high enough to be flicker-free; adjust to the driver).
#define TFT_BACKLIGHT_PWM_HZ 20000u

// --- Datasheet-standard ST7789 commands -----------------------------------------------------
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_NORON 0x13
#define ST7789_INVON 0x21
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2A
#define ST7789_RASET 0x2B
#define ST7789_RAMWR 0x2C
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3A
#define ST7789_COLMOD_RGB565 0x55

static int g_dma_ch = -1;
static bool g_dma_active;   // a blit was started and not yet waited for
static bool g_ramwr_active; // RAMWR sent for the current window, CS held low
static uint g_spi_bits;     // current SPI frame size (8 or 16)

static const hal_pixel_t g_zero_row[TFT_WIDTH]; // zero-filled; used to clear the screen

void hal_display_wait(void);

static inline void cs_select(void) { gpio_put(TFT_PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(TFT_PIN_CS, 1); }

static void spi_frame_bits(uint bits) {
    if (g_spi_bits != bits) {
        // spi_set_format disables the SPI, so make sure the FIFO has drained first.
        while (spi_is_busy(TFT_SPI)) {
            tight_loop_contents();
        }
        spi_set_format(TFT_SPI, bits, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        g_spi_bits = bits;
    }
}

static void write_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    // Any command ends an open RAMWR pixel stream: finish in-flight DMA, then CS is cycled below.
    hal_display_wait();
    g_ramwr_active = false;
    spi_frame_bits(8);
    cs_select();
    gpio_put(TFT_PIN_DC, 0);
    spi_write_blocking(TFT_SPI, &cmd, 1);
    if (len > 0) {
        gpio_put(TFT_PIN_DC, 1);
        spi_write_blocking(TFT_SPI, data, len);
    }
    cs_deselect();
}

static void write_cmd1(uint8_t cmd, uint8_t arg) { write_cmd(cmd, &arg, 1); }

void hal_display_wait(void) {
    if (g_dma_active) {
        dma_channel_wait_for_finish_blocking((uint)g_dma_ch);
        g_dma_active = false;
    }
    // DMA done only means the FIFO was fed; wait for the last bits to leave the shifter.
    while (spi_is_busy(TFT_SPI)) {
        tight_loop_contents();
    }
}

static void backlight_pwm_init(void) {
    gpio_set_function(TFT_PIN_BACKLIGHT, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(TFT_PIN_BACKLIGHT);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 255);
    pwm_config_set_clkdiv(&cfg,
                          (float)clock_get_hz(clk_sys) / (256.0f * (float)TFT_BACKLIGHT_PWM_HZ));
    pwm_init(slice, &cfg, true);
    hal_display_backlight(0);
}

void hal_display_backlight(uint8_t level) {
    uint16_t duty = TFT_BACKLIGHT_ACTIVE_HIGH ? level : (uint16_t)(255u - level);
    pwm_set_gpio_level(TFT_PIN_BACKLIGHT, duty);
}

hal_status_t hal_display_init(void) {
    // Hold the unused SD card select high and CS deasserted before touching the bus.
    gpio_init(TFT_PIN_SD_CS);
    gpio_set_dir(TFT_PIN_SD_CS, GPIO_OUT);
    gpio_put(TFT_PIN_SD_CS, 1);
    gpio_init(TFT_PIN_CS);
    gpio_set_dir(TFT_PIN_CS, GPIO_OUT);
    gpio_put(TFT_PIN_CS, 1);
    gpio_init(TFT_PIN_DC);
    gpio_set_dir(TFT_PIN_DC, GPIO_OUT);
    gpio_init(TFT_PIN_RST);
    gpio_set_dir(TFT_PIN_RST, GPIO_OUT);
    gpio_put(TFT_PIN_RST, 1);

    backlight_pwm_init();

    uint actual = spi_init(TFT_SPI, TFT_SPI_BAUD_HZ);
    printf("display: SPI1 requested %u Hz, actual %u Hz\n", (unsigned)TFT_SPI_BAUD_HZ, actual);
    gpio_set_function(TFT_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_PIN_MOSI, GPIO_FUNC_SPI);
    // MISO is never read (no readback), but keeping it on SPI1 is harmless and matches the plan.
    gpio_set_function(TFT_PIN_MISO, GPIO_FUNC_SPI);
    g_spi_bits = 0;
    spi_frame_bits(8);

    if (g_dma_ch < 0) {
        g_dma_ch = dma_claim_unused_channel(false);
        if (g_dma_ch < 0) {
            return HAL_EBUSY;
        }
    }
    dma_channel_config dc = dma_channel_get_default_config((uint)g_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_16);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, spi_get_dreq(TFT_SPI, true));
    dma_channel_set_config((uint)g_dma_ch, &dc, false);
    dma_channel_set_write_addr((uint)g_dma_ch, &spi_get_hw(TFT_SPI)->dr, false);
    g_dma_active = false;
    g_ramwr_active = false;

    // Hardware reset. Timings are the generic ST7789 figures (reset pulse >= 10 us,
    // ~120 ms before the next command); generous here.
    gpio_put(TFT_PIN_RST, 1);
    sleep_ms(10);
    gpio_put(TFT_PIN_RST, 0);
    sleep_ms(20);
    gpio_put(TFT_PIN_RST, 1);
    sleep_ms(120);

    write_cmd(ST7789_SWRESET, NULL, 0);
    sleep_ms(150);
    write_cmd(ST7789_SLPOUT, NULL, 0);
    sleep_ms(120);
    write_cmd1(ST7789_COLMOD, ST7789_COLMOD_RGB565);
    write_cmd1(ST7789_MADCTL, TFT_MADCTL_LANDSCAPE);
    // TODO(hw): UNCONFIRMED. Inversion on is typical for IPS ST7789 panels; verify colours.
    write_cmd(ST7789_INVON, NULL, 0);
    write_cmd(ST7789_NORON, NULL, 0);
    sleep_ms(10);

    // Clear controller RAM before turning the panel on so there is no flash of noise.
    hal_status_t st = hal_display_set_window(0, 0, TFT_WIDTH, TFT_HEIGHT);
    for (uint16_t y = 0; y < TFT_HEIGHT && st == HAL_OK; y++) {
        st = hal_display_blit(g_zero_row, TFT_WIDTH);
    }
    hal_display_wait();
    if (st != HAL_OK) {
        return st;
    }

    write_cmd(ST7789_DISPON, NULL, 0);
    sleep_ms(20);
    return HAL_OK;
}

uint16_t hal_display_width(void) { return TFT_WIDTH; }
uint16_t hal_display_height(void) { return TFT_HEIGHT; }

static void write_range(uint8_t cmd, uint16_t start, uint16_t end) {
    uint8_t d[4] = {(uint8_t)(start >> 8), (uint8_t)start, (uint8_t)(end >> 8), (uint8_t)end};
    write_cmd(cmd, d, sizeof d);
}

hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0 || (uint32_t)x + w > TFT_WIDTH || (uint32_t)y + h > TFT_HEIGHT) {
        return HAL_EINVAL;
    }
    hal_display_wait();
    cs_deselect(); // ends any RAMWR stream left open by a previous blit
    g_ramwr_active = false;
    write_range(ST7789_CASET, (uint16_t)(x + TFT_COL_OFFSET), (uint16_t)(x + w - 1 + TFT_COL_OFFSET));
    write_range(ST7789_RASET, (uint16_t)(y + TFT_ROW_OFFSET), (uint16_t)(y + h - 1 + TFT_ROW_OFFSET));
    return HAL_OK;
}

hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count) {
    if (pixels == NULL) {
        return HAL_EINVAL;
    }
    if (count == 0) {
        return HAL_OK;
    }
    hal_display_wait(); // previous blit (if any) must finish before the bus is reused

    if (!g_ramwr_active) {
        // RAMWR opens the pixel stream; CS stays low (no deselect) until the next command.
        spi_frame_bits(8);
        cs_select();
        gpio_put(TFT_PIN_DC, 0);
        uint8_t cmd = ST7789_RAMWR;
        spi_write_blocking(TFT_SPI, &cmd, 1);
        g_ramwr_active = true;
    }
    cs_select(); // idempotent; guarantees CS is low for the pixel stream
    spi_frame_bits(16);
    gpio_put(TFT_PIN_DC, 1);

    g_dma_active = true;
    dma_channel_transfer_from_buffer_now((uint)g_dma_ch, pixels, (uint32_t)count);
    return HAL_OK;
}

void hal_display_present(void) {
    // The ST7789 has its own frame memory; nothing to flip.
}
