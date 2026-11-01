// SPDX-License-Identifier: MPL-2.0
/*
 * Host display: an in-memory RGB565 framebuffer that emulates controller frame memory
 * (window + auto-incrementing write cursor, wrapping inside the window like an ST7789).
 * present() shows it in the SDL window if available; if NEUPOD_DISPLAY_PPM is set it also
 * writes the frame to that path as a binary PPM (works headless).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_display.h"
#include "host_internal.h"

#define PANEL_W 320u
#define PANEL_H 240u

static uint16_t g_fb[PANEL_W * PANEL_H];
static uint16_t g_wx, g_wy, g_ww = PANEL_W, g_wh = PANEL_H; // write window
static uint32_t g_cursor;                                    // pixels written into the window
static uint8_t g_backlight = 255;

hal_status_t hal_display_init(void) {
    memset(g_fb, 0, sizeof g_fb);
    g_wx = g_wy = 0;
    g_ww = PANEL_W;
    g_wh = PANEL_H;
    g_cursor = 0;
    (void)host_window_open(PANEL_W, PANEL_H);
    hal_display_present();
    return HAL_OK;
}

uint16_t hal_display_width(void) { return PANEL_W; }
uint16_t hal_display_height(void) { return PANEL_H; }

hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0 || (uint32_t)x + w > PANEL_W || (uint32_t)y + h > PANEL_H) {
        return HAL_EINVAL;
    }
    g_wx = x;
    g_wy = y;
    g_ww = w;
    g_wh = h;
    g_cursor = 0;
    return HAL_OK;
}

hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count) {
    if (pixels == NULL) {
        return HAL_EINVAL;
    }
    uint32_t total = (uint32_t)g_ww * g_wh;
    for (size_t i = 0; i < count; i++) {
        uint32_t c = g_cursor % total;
        g_fb[(size_t)(g_wy + c / g_ww) * PANEL_W + g_wx + c % g_ww] = pixels[i];
        g_cursor = c + 1;
    }
    return HAL_OK;
}

void hal_display_wait(void) {}

static void dump_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "neuPod host: cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", PANEL_W, PANEL_H);
    for (size_t i = 0; i < PANEL_W * PANEL_H; i++) {
        uint16_t p = g_fb[i];
        // Expand RGB565 to 8 bits, replicating the top bits into the low bits.
        uint8_t rgb[3] = {(uint8_t)(((p >> 11) << 3) | (p >> 13)),
                          (uint8_t)((((p >> 5) & 0x3F) << 2) | ((p >> 9) & 3)),
                          (uint8_t)(((p & 0x1F) << 3) | ((p >> 2) & 7))};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

void hal_display_present(void) {
    host_window_present(g_fb, PANEL_W, PANEL_H, g_backlight);
    const char *ppm = getenv("NEUPOD_DISPLAY_PPM");
    if (ppm != NULL && ppm[0] != '\0') {
        dump_ppm(ppm);
    }
}

void hal_display_backlight(uint8_t level) { g_backlight = level; }
