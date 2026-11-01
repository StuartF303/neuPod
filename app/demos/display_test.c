// SPDX-License-Identifier: MPL-2.0
#include "display_test.h"

#include "hal_display.h"

#define LINES_PER_BLIT 8u
#define MAX_WIDTH 480u // row buffer size; panels wider than this are drawn clipped

// White, yellow, cyan, green, magenta, red, blue, black (classic SMPTE-ish order).
static const uint8_t k_bars[8][3] = {
    {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
    {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
};

// Double-buffered so the next band can be rendered while DMA sends the previous one.
static hal_pixel_t g_band[2][MAX_WIDTH * LINES_PER_BLIT];

typedef void (*row_fill_fn)(hal_pixel_t *row, uint16_t w);

static void fill_bars(hal_pixel_t *row, uint16_t w);
static void fill_gradient(hal_pixel_t *row, uint16_t w);

// Render a band into the idle buffer (the other one may still be in DMA), then send it.
static hal_status_t draw_band(uint16_t y, uint16_t h, uint16_t w, unsigned *buf, row_fill_fn fill) {
    hal_pixel_t *b = g_band[*buf];
    fill(b, w);
    for (uint16_t r = 1; r < h; r++) {
        for (uint16_t x = 0; x < w; x++) {
            b[(size_t)r * w + x] = b[x];
        }
    }
    hal_status_t st = hal_display_set_window(0, y, w, h); // waits for the previous blit
    if (st != HAL_OK) {
        return st;
    }
    st = hal_display_blit(b, (size_t)w * h);
    *buf ^= 1u;
    return st;
}

static hal_status_t draw_section(uint16_t y0, uint16_t y1, uint16_t w, unsigned *buf,
                                 row_fill_fn fill) {
    for (uint16_t y = y0; y < y1; y = (uint16_t)(y + LINES_PER_BLIT)) {
        uint16_t lines = (uint16_t)((unsigned)(y1 - y) < LINES_PER_BLIT ? (unsigned)(y1 - y) : LINES_PER_BLIT);
        hal_status_t st = draw_band(y, lines, w, buf, fill);
        if (st != HAL_OK) {
            return st;
        }
    }
    return HAL_OK;
}

static void fill_bars(hal_pixel_t *row, uint16_t w) {
    for (uint16_t x = 0; x < w; x++) {
        const uint8_t *c = k_bars[(uint32_t)x * 8u / w];
        row[x] = HAL_RGB565(c[0], c[1], c[2]);
    }
}

static void fill_gradient(hal_pixel_t *row, uint16_t w) {
    for (uint16_t x = 0; x < w; x++) {
        uint8_t v = (uint8_t)((uint32_t)x * 255u / (w > 1 ? w - 1u : 1u));
        row[x] = HAL_RGB565(v, v, v);
    }
}

hal_status_t display_test_draw(void) {
    uint16_t w = hal_display_width();
    uint16_t h = hal_display_height();
    if (w == 0 || h == 0 || w > MAX_WIDTH) {
        return HAL_EINVAL;
    }

    uint16_t bars_h = (uint16_t)(h * 3u / 4u);
    unsigned buf = 0;
    hal_status_t st = draw_section(0, bars_h, w, &buf, fill_bars);
    if (st == HAL_OK) {
        st = draw_section(bars_h, h, w, &buf, fill_gradient);
    }
    hal_display_wait();
    if (st == HAL_OK) {
        hal_display_present();
    }
    return st;
}
