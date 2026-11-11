// SPDX-License-Identifier: MPL-2.0
/*
 * Y2 display: /dev/fb0 (mtkfb, 480x360, 3 pages, XRGB8888). The HAL keeps an RGB565 shadow frame with an
 * ST7789-style window + cursor (so the UI draws exactly as on the other targets); present() converts the rows
 * that changed into the next page of the framebuffer and flips to it (FBIOPAN_DISPLAY then FBIOPUT_VSCREENINFO,
 * about 46 fps measured; a write that is not followed by a pan is not guaranteed to reach the panel).
 * Nothing is flipped when nothing was drawn since the last present, so an idle UI costs no framebuffer traffic.
 * Brightness is /sys/class/leds/lcd-backlight/brightness (0..255); sleep/wake is backlight off/on.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hal_display.h"
#include "hal_system.h"
#include "y2_fb.h"
#include "y2_hal.h"

#define PANEL_W 480u
#define PANEL_H 360u

static uint16_t g_shadow[PANEL_W * PANEL_H]; // RGB565, what the UI has drawn
static uint16_t g_wx, g_wy, g_ww = PANEL_W, g_wh = PANEL_H;
static uint32_t g_cursor; // pixels written into the window
static uint8_t g_madctl = 0x60;
static bool g_inversion = true;

// What the panel would show for stored pixel p, as the host HAL emulates it: BGR order swaps red and blue; with inversion
// OFF the (inversion-needing) panel shows colours inverted. Mirroring (MX/MY/MV) is not emulated.
static uint16_t panel_pixel(uint16_t p) {
    if (g_madctl & 0x08u) {
        p = (uint16_t)(((p & 0x1Fu) << 11) | (p & 0x07E0u) | (p >> 11));
    }
    return g_inversion ? p : (uint16_t)~p;
}

static int g_fd = -1;
static uint8_t *g_map;
static size_t g_map_len;
static struct fb_var_screeninfo g_var;
static uint32_t g_line_bytes;
static y2_pager_t g_pager;
static bool g_dirty;
static bool g_pan_failed_logged;

static int g_bl_fd = -1;
static uint8_t g_level = 255;       // what the UI asked for
static int g_applied_level = -1;    // what was written
static bool g_sleeping;

static void backlight_apply(uint8_t level) {
    if (g_bl_fd < 0 || level == g_applied_level) {
        return;
    }
    char buf[8];
    int n = snprintf(buf, sizeof buf, "%u\n", (unsigned)level);
    if (write(g_bl_fd, buf, (size_t)n) == n) {
        g_applied_level = level;
    }
}

// The FreeRTOS POSIX port's 1 kHz SIGALRM tick can interrupt an ioctl (EINTR): retry, never count it as a failure.
static int ioctl_retry(unsigned long req, void *arg) {
    int r;
    do {
        r = ioctl(g_fd, req, arg);
    } while (r != 0 && errno == EINTR);
    return r;
}

static bool pan_to(unsigned page) {
    g_var.xoffset = 0;
    g_var.yoffset = page * PANEL_H;
    if (ioctl_retry(FBIOPAN_DISPLAY, &g_var) != 0) {
        return false;
    }
    g_var.activate = FB_ACTIVATE_NOW;
    return ioctl_retry(FBIOPUT_VSCREENINFO, &g_var) == 0;
}

hal_status_t hal_display_init(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    if (g_map != NULL) {
        munmap(g_map, g_map_len);
        g_map = NULL;
    }
    memset(g_shadow, 0, sizeof g_shadow);
    g_wx = g_wy = 0;
    g_ww = PANEL_W;
    g_wh = PANEL_H;
    g_cursor = 0;
    g_madctl = 0x60;
    g_inversion = true;
    g_sleeping = false;

    g_fd = open(Y2_FB_DEVICE, O_RDWR | O_CLOEXEC);
    if (g_fd < 0) {
        printf("display: cannot open %s: %s\n", Y2_FB_DEVICE, strerror(errno));
        return HAL_ENODEV;
    }
    struct fb_fix_screeninfo fix;
    if (ioctl_retry(FBIOGET_VSCREENINFO, &g_var) != 0 || ioctl_retry(FBIOGET_FSCREENINFO, &fix) != 0) {
        printf("display: FBIOGET_*SCREENINFO failed: %s\n", strerror(errno));
        goto fail;
    }
    if (g_var.xres != PANEL_W || g_var.yres != PANEL_H ||
        !y2_fb_mode_is_xrgb8888(g_var.bits_per_pixel, (y2_fb_field_t){g_var.red.offset, g_var.red.length},
                                (y2_fb_field_t){g_var.green.offset, g_var.green.length},
                                (y2_fb_field_t){g_var.blue.offset, g_var.blue.length},
                                (y2_fb_field_t){g_var.transp.offset, g_var.transp.length})) {
        printf("display: unexpected mode %ux%u %ubpp (want %ux%u XRGB8888)\n", (unsigned)g_var.xres, (unsigned)g_var.yres,
               (unsigned)g_var.bits_per_pixel, PANEL_W, PANEL_H);
        goto fail;
    }
    g_line_bytes = fix.line_length;
    g_map_len = fix.smem_len;
    g_map = mmap(NULL, g_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
    if (g_map == MAP_FAILED) {
        g_map = NULL;
        printf("display: mmap failed: %s\n", strerror(errno));
        goto fail;
    }
    unsigned pages = y2_fb_page_count(g_map_len, g_var.yres_virtual, g_line_bytes, g_var.yres);
    // A previous run can leave yoffset on another page: pan to page 0 so the state we track is the real one.
    if (!pan_to(0)) {
        printf("display: could not pan to page 0 (%s); single page mode\n", strerror(errno));
        pages = 1;
    }
    y2_pager_init(&g_pager, pages, 0, PANEL_H);
    g_bl_fd = g_bl_fd >= 0 ? g_bl_fd : open(Y2_BACKLIGHT, O_WRONLY | O_CLOEXEC);
    if (g_bl_fd < 0) {
        printf("display: no backlight control at %s: %s\n", Y2_BACKLIGHT, strerror(errno));
    }
    g_applied_level = -1;
    g_pan_failed_logged = false;
    printf("display: %s %ux%u XRGB8888, %u page(s), line %u bytes\n", fix.id, PANEL_W, PANEL_H, pages, (unsigned)g_line_bytes);
    g_dirty = true;
    hal_display_present(); // black, so nothing stale from a previous run is left on the panel
    return HAL_OK;

fail:
    close(g_fd);
    g_fd = -1;
    return HAL_ENODEV;
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
    if (count == 0) {
        return HAL_OK;
    }
    const uint32_t total = (uint32_t)g_ww * g_wh;
    // Rows this call touches: the whole window if it wraps, else from the cursor's row to the last pixel's row.
    uint32_t start = g_cursor % total;
    uint32_t first_row = 0, last_row = g_wh - 1u;
    if (count <= total - start) {
        first_row = start / g_ww;
        last_row = (uint32_t)((start + count - 1u) / g_ww);
    }
    y2_pager_mark(&g_pager, (uint16_t)(g_wy + first_row), (uint16_t)(g_wy + last_row + 1u));
    g_dirty = true;
    size_t left = count;
    while (left > 0) {
        uint32_t c = g_cursor % total;
        uint32_t row = c / g_ww, col = c % g_ww;
        size_t n = g_ww - col;
        if (n > left) {
            n = left;
        }
        memcpy(&g_shadow[(size_t)(g_wy + row) * PANEL_W + g_wx + col], pixels, n * sizeof *pixels);
        pixels += n;
        left -= n;
        g_cursor = (c + (uint32_t)n) % total;
    }
    return HAL_OK;
}

hal_status_t hal_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal_pixel_t colour) {
    hal_status_t st = hal_display_set_window(x, y, w, h);
    if (st != HAL_OK) {
        return st;
    }
    for (uint16_t r = 0; r < h; r++) {
        uint16_t *row = &g_shadow[(size_t)(y + r) * PANEL_W + x];
        for (uint16_t c = 0; c < w; c++) {
            row[c] = colour;
        }
    }
    y2_pager_mark(&g_pager, y, (uint16_t)(y + h));
    g_dirty = true;
    g_cursor = 0;
    return HAL_OK;
}

void hal_display_sleep(bool sleep) {
    if (sleep == g_sleeping) {
        return;
    }
    g_sleeping = sleep;
    backlight_apply(sleep ? 0 : g_level);
    printf("display: %s\n", sleep ? "sleep (backlight off)" : "wake");
}

// Bring-up menus: BGR and inversion are emulated in the conversion (like the host HAL), so they look the same.
void hal_display_set_madctl(uint8_t madctl) {
    g_madctl = madctl;
    y2_pager_mark_all(&g_pager);
    g_dirty = true;
    hal_display_present();
}
uint8_t hal_display_get_madctl(void) { return g_madctl; }
void hal_display_set_inversion(bool on) {
    g_inversion = on;
    y2_pager_mark_all(&g_pager);
    g_dirty = true;
    hal_display_present();
}
bool hal_display_get_inversion(void) { return g_inversion; }

void hal_display_wait(void) {}
void hal_display_service(void) {
    if (g_dirty) { // a flip that failed earlier (or a bring-up change): try again
        hal_display_present();
    }
}

void hal_display_present(void) {
    if (!g_dirty || g_map == NULL) {
        return;
    }
    g_dirty = false;
    unsigned page;
    y2_rows_t rows;
    if (!y2_pager_next(&g_pager, &page, &rows)) {
        return;
    }
    uint8_t *dst = g_map + (size_t)page * PANEL_H * g_line_bytes;
    const bool emulate = (g_madctl & 0x08u) != 0 || !g_inversion;
    for (uint32_t y = rows.lo; y < rows.hi; y++) {
        const uint16_t *src = &g_shadow[(size_t)y * PANEL_W];
        uint32_t *row = (uint32_t *)(void *)(dst + (size_t)y * g_line_bytes);
        if (emulate) {
            uint16_t tmp[PANEL_W];
            for (uint32_t x = 0; x < PANEL_W; x++) {
                tmp[x] = panel_pixel(src[x]);
            }
            y2_fb_convert_rgb565(tmp, row, PANEL_W);
        } else {
            y2_fb_convert_rgb565(src, row, PANEL_W);
        }
    }
    if (pan_to(page)) {
        y2_pager_commit(&g_pager, page); // only now is it the visible page
        g_pan_failed_logged = false;
        return;
    }
    // The flip failed: nothing was committed, so the same rows are converted again. Stay dirty; hal_display_service()
    // (called every UI pass) retries without waiting for another LVGL flush.
    if (!g_pan_failed_logged) {
        g_pan_failed_logged = true;
        printf("display: page flip failed: %s (will retry)\n", strerror(errno));
    }
    g_dirty = true;
}

void hal_display_backlight(uint8_t level) {
    g_level = level;
    if (!g_sleeping) {
        backlight_apply(level);
    }
}
