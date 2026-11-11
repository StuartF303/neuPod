// SPDX-License-Identifier: MPL-2.0
/*
 * Host display: an in-memory RGB565 framebuffer that emulates controller frame memory
 * (window + auto-incrementing write cursor, wrapping inside the window like an ST7789).
 * present() shows it in the SDL window if available; if NEUPOD_DISPLAY_PPM is set it also
 * writes the frame to that path as a binary PPM (works headless).
 *
 * The panel size is 320x240 unless NEUPOD_DISPLAY_SIZE=WxH is set (read once, at the first call): the host can then
 * stand in for a bigger panel (480x360 for the Y2 testbed) so the UI's resolution profiles can be run and tested here.
 * Sizes outside 320x240 .. 640x480 are refused with a message and the default is used.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_display.h"
#include "hal_host.h"
#include "host_internal.h"

#define PANEL_DEFAULT_W 320u
#define PANEL_DEFAULT_H 240u
#define PANEL_MAX_W 640u
#define PANEL_MAX_H 480u

static uint16_t g_pw = PANEL_DEFAULT_W, g_ph = PANEL_DEFAULT_H; // panel size (NEUPOD_DISPLAY_SIZE)
static bool g_size_read;
static uint16_t g_fb[PANEL_MAX_W * PANEL_MAX_H];
static uint16_t g_shown[PANEL_MAX_W * PANEL_MAX_H]; // compose() output, shared by present() and the snapshot dump
static uint16_t g_wx, g_wy, g_ww = PANEL_DEFAULT_W, g_wh = PANEL_DEFAULT_H; // write window
static uint32_t g_cursor;                                    // pixels written into the window
static uint8_t g_backlight = 255;
static uint8_t g_madctl = 0x60; // matches the rp2350 default (landscape placeholder, RGB order)
static bool g_inversion = true; // panel needs INVON; "off" shows inverted colours
static bool g_sleeping;

// What the panel would show for stored pixel p: BGR order swaps red/blue; with inversion OFF
// the (inversion-needing) panel shows colours inverted. Mirroring (MX/MY/MV) is not emulated.
static uint16_t panel_pixel(uint16_t p) {
    if (g_madctl & 0x08u) {
        p = (uint16_t)(((p & 0x1Fu) << 11) | (p & 0x07E0u) | (p >> 11));
    }
    return g_inversion ? p : (uint16_t)~p;
}

// Parse NEUPOD_DISPLAY_SIZE once; anything unusable keeps the 320x240 default.
static void read_size(void) {
    if (g_size_read) {
        return;
    }
    g_size_read = true;
    const char *env = getenv("NEUPOD_DISPLAY_SIZE");
    if (env == NULL || env[0] == '\0') {
        return;
    }
    unsigned w = 0, h = 0;
    char tail = 0;
    if (sscanf(env, "%ux%u%c", &w, &h, &tail) != 2 || w < PANEL_DEFAULT_W || h < PANEL_DEFAULT_H || w > PANEL_MAX_W || h > PANEL_MAX_H) {
        fprintf(stderr, "neuPod host: NEUPOD_DISPLAY_SIZE='%s' not usable (want WxH within %ux%u..%ux%u); using %ux%u\n", env, PANEL_DEFAULT_W,
                PANEL_DEFAULT_H, PANEL_MAX_W, PANEL_MAX_H, PANEL_DEFAULT_W, PANEL_DEFAULT_H);
        return;
    }
    g_pw = (uint16_t)w;
    g_ph = (uint16_t)h;
}

static void compose(uint16_t *out) {
    for (size_t i = 0; i < (size_t)g_pw * g_ph; i++) {
        out[i] = g_sleeping ? 0 : panel_pixel(g_fb[i]);
    }
}

hal_status_t hal_display_init(void) {
    read_size();
    memset(g_fb, 0, sizeof g_fb);
    g_wx = g_wy = 0;
    g_ww = g_pw;
    g_wh = g_ph;
    g_cursor = 0;
    g_madctl = 0x60;
    g_inversion = true;
    g_sleeping = false;
    (void)host_window_open(g_pw, g_ph);
    hal_display_present();
    return HAL_OK;
}

uint16_t hal_display_width(void) {
    read_size();
    return g_pw;
}
uint16_t hal_display_height(void) {
    read_size();
    return g_ph;
}

hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0 || (uint32_t)x + w > g_pw || (uint32_t)y + h > g_ph) {
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
        g_fb[(size_t)(g_wy + c / g_ww) * g_pw + g_wx + c % g_ww] = pixels[i];
        g_cursor = c + 1;
    }
    return HAL_OK;
}

hal_status_t hal_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                   hal_pixel_t colour) {
    hal_status_t st = hal_display_set_window(x, y, w, h);
    if (st != HAL_OK) {
        return st;
    }
    for (uint16_t r = 0; r < h; r++) {
        for (uint16_t c = 0; c < w; c++) {
            g_fb[(size_t)(y + r) * g_pw + x + c] = colour;
        }
    }
    g_cursor = 0;
    return HAL_OK;
}

void hal_display_sleep(bool sleep) {
    g_sleeping = sleep;
    printf("host display: %s\n", sleep ? "sleep (window blanked)" : "wake");
    hal_display_present();
}

void hal_display_set_madctl(uint8_t madctl) {
    g_madctl = madctl;
    hal_display_present();
}
uint8_t hal_display_get_madctl(void) { return g_madctl; }
void hal_display_set_inversion(bool on) {
    g_inversion = on;
    hal_display_present();
}
bool hal_display_get_inversion(void) { return g_inversion; }

void hal_display_wait(void) {}

static bool dump_ppm(const char *path, const uint16_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "neuPod host: cannot write %s\n", path);
        return false;
    }
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)g_pw, (unsigned)g_ph);
    for (size_t i = 0; i < (size_t)g_pw * g_ph; i++) {
        uint16_t p = fb[i];
        // Expand RGB565 to 8 bits, replicating the top bits into the low bits.
        uint8_t rgb[3] = {(uint8_t)(((p >> 11) << 3) | (p >> 13)),
                          (uint8_t)((((p >> 5) & 0x3F) << 2) | ((p >> 9) & 3)),
                          (uint8_t)(((p & 0x1F) << 3) | ((p >> 2) & 7))};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

bool hal_host_display_dump_ppm(const char *path) {
    compose(g_shown);
    return dump_ppm(path, g_shown);
}

void hal_display_service(void) { host_pump_events(); }

void hal_display_present(void) {
    compose(g_shown);
    host_window_present(g_shown, g_pw, g_ph, g_backlight);
    const char *ppm = getenv("NEUPOD_DISPLAY_PPM");
    if (ppm != NULL && ppm[0] != '\0') {
        (void)dump_ppm(ppm, g_shown);
    }
}

void hal_display_backlight(uint8_t level) {
    g_backlight = level;
    hal_display_present(); // the window shows the level only when redrawn
}
