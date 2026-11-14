// SPDX-License-Identifier: MPL-2.0
// The panel init sequence below was written for this probe from the register values the factory demo
// uses (they are the panel's functional configuration); no Waveshare source file is copied here.
#include "display.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "results.h"

static const char *TAG = "disp";

#define LCD_HOST        SPI2_HOST
#define LCD_PCLK_HZ     (40 * 1000 * 1000)   // QSPI clock; 4 data lines
#define STRIP_PX        9600                 // bounce buffer: 16 landscape rows
#define FB_ROWS         464                  // 450 padded to a multiple of 16

// Panel byte order is big-endian RGB565; store the framebuffer already swapped.
#define RGB(r, g, b) ((uint16_t)__builtin_bswap16((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))))

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb;                 // 600 x 464 RGB565 in PSRAM (landscape)
static uint16_t *s_bounce[2];          // internal DMA-capable strips
static SemaphoreHandle_t s_done;       // given by the colour-transfer-done callback
static bool s_up;
static int s_mode;                     // 0 = hardware landscape, 1/2 = software rotate A/B
static uint8_t s_tmap;                 // touch variant bits: 1 swap_xy, 2 mirror_x, 4 mirror_y

static const char *const MODE_NAMES[] = {
    "hardware landscape (MADCTL 0x30)", "software rotate A (matches hardware)", "software rotate B (180 deg from A)",
};

// RM690B0 init: {cmd, data, length, delay ms}. No 0x29 here: the display is switched on after the first frame.
static const sh8601_lcd_init_cmd_t INIT_CMDS[] = {
    { 0xFE, (uint8_t[]){ 0x20 }, 1, 0 },    // user command set 1
    { 0x26, (uint8_t[]){ 0x0A }, 1, 0 },
    { 0x24, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0xFE, (uint8_t[]){ 0x00 }, 1, 0 },    // back to the standard command set
    { 0x3A, (uint8_t[]){ 0x55 }, 1, 0 },    // 16 bit/pixel
    { 0xC2, (uint8_t[]){ 0x00 }, 1, 10 },
    { 0x35, NULL, 0, 0 },                   // tearing effect line on
    { 0x51, (uint8_t[]){ 0x00 }, 1, 10 },   // brightness 0 until the first frame is in
    { 0x11, NULL, 0, 80 },                  // sleep out
    { 0x2A, (uint8_t[]){ 0x00, 0x10, 0x01, 0xD1 }, 4, 0 },   // columns 16..465
    { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x02, 0x57 }, 4, 0 },   // rows 0..599
};

// 5x7 glyphs for the characters the banner needs (one row per byte, bit 4 = leftmost pixel).
static const struct { char c; uint8_t r[7]; } FONT[] = {
    { 'n', { 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11 } },
    { 'e', { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E } },
    { 'u', { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'o', { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E } },
    { 'd', { 0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { 'x', { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11 } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
};

// ---- framebuffer drawing (landscape coordinates) ----

static void fb_rect(int x, int y, int w, int h, uint16_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LAND_W) w = LAND_W - x;
    if (y + h > LAND_H) h = LAND_H - y;
    for (int j = 0; j < h; j++) {
        uint16_t *p = s_fb + (y + j) * LAND_W + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

static void fb_text(int x, int y, int sc, const char *s, uint16_t fg)
{
    for (; *s; s++, x += 6 * sc) {
        for (size_t g = 0; g < sizeof FONT / sizeof FONT[0]; g++) {
            if (FONT[g].c != *s) continue;
            for (int r = 0; r < 7; r++)
                for (int col = 0; col < 5; col++)
                    if (FONT[g].r[r] & (0x10 >> col)) fb_rect(x + col * sc, y + r * sc, sc, sc, fg);
        }
    }
}

// ---- pushing to the panel ----

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

// Copy a panel-space rectangle strip (pw wide, n rows from y0) of the landscape framebuffer into dst.
static void fill_strip(uint16_t *dst, int x0, int y0, int pw, int n)
{
    switch (s_mode) {
    case 0:
        for (int r = 0; r < n; r++) memcpy(dst + r * pw, s_fb + (y0 + r) * LAND_W + x0, pw * 2);
        break;
    case 1:   // panel (px,py) <- landscape (599-py, px)
        for (int c = 0; c < pw; c++) {
            const uint16_t *src = s_fb + (x0 + c) * LAND_W;
            for (int r = 0; r < n; r++) dst[r * pw + c] = src[LAND_W - 1 - (y0 + r)];
        }
        break;
    default:  // panel (px,py) <- landscape (py, 449-px)
        for (int c = 0; c < pw; c++) {
            const uint16_t *src = s_fb + (LAND_H - 1 - (x0 + c)) * LAND_W;
            for (int r = 0; r < n; r++) dst[r * pw + c] = src[y0 + r];
        }
        break;
    }
}

// Inclusive, even-aligned rectangle in panel space (before the 16-pixel RAM offset).
static bool push_panel_rect(int x0, int y0, int x1, int y1)
{
    int pw = x1 - x0 + 1;
    int rows = (STRIP_PX / pw) & ~1;
    if (rows < 2) return false;
    while (xSemaphoreTake(s_done, 0) == pdTRUE) {}   // drop stale completions
    int inflight = 0, buf = 0;
    bool ok = true;
    for (int y = y0; y <= y1 && ok; y += rows) {
        int n = (y1 - y + 1 < rows) ? (y1 - y + 1) : rows;
        if (inflight == 2) {
            ok = xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) == pdTRUE;
            inflight--;
        }
        fill_strip(s_bounce[buf], x0, y, pw, n);
        int ox = (s_mode == 0) ? 0 : PANEL_OFFSET, oy = (s_mode == 0) ? PANEL_OFFSET : 0;
        if (esp_lcd_panel_draw_bitmap(s_panel, x0 + ox, y + oy, x1 + 1 + ox, y + n + oy, s_bounce[buf]) != ESP_OK) ok = false;
        inflight++;
        buf ^= 1;
    }
    while (inflight-- > 0) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) ok = false;
    }
    return ok;
}

static bool push_land_rect(int lx, int ly, int w, int h)
{
    int x0, y0, x1, y1;
    switch (s_mode) {
    case 0:  x0 = lx; x1 = lx + w - 1; y0 = ly; y1 = ly + h - 1; break;
    case 1:  x0 = ly; x1 = ly + h - 1; y0 = LAND_W - 1 - (lx + w - 1); y1 = LAND_W - 1 - lx; break;
    default: x0 = LAND_H - 1 - (ly + h - 1); x1 = LAND_H - 1 - ly; y0 = lx; y1 = lx + w - 1; break;
    }
    int maxx = (s_mode == 0) ? LAND_W - 1 : PANEL_W - 1, maxy = (s_mode == 0) ? LAND_H - 1 : PANEL_H - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > maxx) x1 = maxx;
    if (y1 > maxy) y1 = maxy;
    if (x1 < x0 || y1 < y0) return true;
    x0 &= ~1; y0 &= ~1; x1 |= 1; y1 |= 1;   // the controller wants even start / odd end
    return push_panel_rect(x0, y0, x1, y1);
}

static bool push_all(void) { return push_land_rect(0, 0, LAND_W, LAND_H); }

// ---- commands ----

static esp_err_t panel_cmd(uint8_t cmd, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (cmd << 8), data, len);
}

static void apply_mode(void)
{
    uint8_t madctl = (s_mode == 0) ? 0x30 : 0x00;
    panel_cmd(0x36, &madctl, 1);
    s_tmap = (s_mode == 2) ? 3 : 5;
}

void disp_set_brightness(uint8_t v)
{
    if (s_up) panel_cmd(0x51, &v, 1);
}

bool disp_init(void)
{
    s_done = xSemaphoreCreateCounting(16, 0);
    s_fb = heap_caps_aligned_alloc(64, LAND_W * FB_ROWS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < 2; i++)
        s_bounce[i] = heap_caps_aligned_alloc(64, STRIP_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_done || !s_fb || !s_bounce[0] || !s_bounce[1]) {
        res_set(R_DISP, ST_FAIL, "out of memory (fb %p, bounce %p/%p)", (void *)s_fb, (void *)s_bounce[0], (void *)s_bounce[1]);
        return false;
    }
    memset(s_fb, 0, LAND_W * FB_ROWS * 2);

    spi_bus_config_t bus = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, 32768);
    esp_err_t e = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) { res_set(R_DISP, ST_FAIL, "spi_bus_initialize: %s", esp_err_to_name(e)); return false; }

    esp_lcd_panel_io_spi_config_t io_cfg = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_done, NULL);
    io_cfg.pclk_hz = LCD_PCLK_HZ;
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);
    if (e != ESP_OK) { res_set(R_DISP, ST_FAIL, "panel io: %s", esp_err_to_name(e)); return false; }

    sh8601_vendor_config_t vc = {
        .init_cmds = INIT_CMDS,
        .init_cmds_size = sizeof INIT_CMDS / sizeof INIT_CMDS[0],
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vc,
    };
    e = esp_lcd_new_panel_sh8601(s_io, &pc, &s_panel);
    if (e == ESP_OK) e = esp_lcd_panel_reset(s_panel);
    if (e == ESP_OK) e = esp_lcd_panel_init(s_panel);
    if (e != ESP_OK) { res_set(R_DISP, ST_FAIL, "panel create/reset/init: %s", esp_err_to_name(e)); return false; }
    s_up = true;

    // The SH8601 driver does not implement swap_xy (and mirror_y), so landscape comes from MADCTL directly.
    esp_err_t swap_rc = esp_lcd_panel_swap_xy(s_panel, true);
    s_mode = 0;
    apply_mode();
    res_set(R_DISP, ST_PASS, "init ok, %s; esp_lcd swap_xy -> %s; fb %u B PSRAM, 2x%u B DMA strips, QSPI %d MHz",
            MODE_NAMES[s_mode], esp_err_to_name(swap_rc), (unsigned)(LAND_W * FB_ROWS * 2), STRIP_PX * 2, LCD_PCLK_HZ / 1000000);
    return true;
}

void disp_test_pattern(void)
{
    if (!s_up) return;
    static const uint8_t bars[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 0, 0, 0 },
    };
    int barh = LAND_H * 2 / 3;
    for (int i = 0; i < 8; i++)
        fb_rect(i * (LAND_W / 8), 0, LAND_W / 8, barh, RGB(bars[i][0], bars[i][1], bars[i][2]));
    for (int x = 0; x < LAND_W; x++) {                      // grey ramp
        uint8_t g = (uint8_t)(x * 255 / (LAND_W - 1));
        fb_rect(x, barh, 1, LAND_H - barh, RGB(g, g, g));
    }
    fb_rect(100, 160, 400, 70, RGB(0, 0, 0));               // banner: 20 chars x 18 px at scale 3 = 360 px
    fb_text(100 + (400 - 360) / 2, 160 + (70 - 21) / 2, 3, "neuPod ESP32 600x450", RGB(255, 255, 255));
    // 1-px border, then orientation marks: TL red, TR green, BL blue, BR white.
    fb_rect(0, 0, LAND_W, 1, RGB(255, 255, 255));
    fb_rect(0, LAND_H - 1, LAND_W, 1, RGB(255, 255, 255));
    fb_rect(0, 0, 1, LAND_H, RGB(255, 255, 255));
    fb_rect(LAND_W - 1, 0, 1, LAND_H, RGB(255, 255, 255));
    fb_rect(2, 2, 24, 24, RGB(255, 0, 0));
    fb_rect(LAND_W - 26, 2, 24, 24, RGB(0, 255, 0));
    fb_rect(2, LAND_H - 26, 24, 24, RGB(0, 0, 255));
    fb_rect(LAND_W - 26, LAND_H - 26, 24, 24, RGB(255, 255, 255));
    bool ok = push_all();
    static bool first = true;
    if (first) {
        first = false;
        esp_lcd_panel_disp_on_off(s_panel, true);
        vTaskDelay(pdMS_TO_TICKS(20));
        disp_set_brightness(0xFF);
    }
    if (!ok) ESP_LOGW(TAG, "test pattern push failed");
}

void disp_measure(void)
{
    if (!s_up) { res_set(R_FPS, ST_FAIL, "no display"); return; }
    const int frames = 10;
    int64_t t0 = esp_timer_get_time();
    bool ok = true;
    for (int i = 0; i < frames && ok; i++) ok = push_all();
    int64_t dt = esp_timer_get_time() - t0;
    double ms = dt / 1000.0 / frames;
    res_set(R_FPS, ok ? ST_PASS : ST_FAIL, "%d full 600x450 frames (540000 B) in %.1f ms each = %.1f fps, %.1f MB/s, mode: %s",
            frames, ms, 1000.0 / ms, 540000.0 / (ms * 1000.0), MODE_NAMES[s_mode]);
}

void disp_brightness_test(void)
{
    if (!s_up) { res_set(R_BRIGHT, ST_FAIL, "no display"); return; }
    uint8_t v = 0x80;
    esp_err_t e1 = panel_cmd(0x51, &v, 1);
    vTaskDelay(pdMS_TO_TICKS(1500));
    v = 0xFF;
    esp_err_t e2 = panel_cmd(0x51, &v, 1);
    res_set(R_BRIGHT, (e1 || e2) ? ST_FAIL : ST_PASS, "0x51 = 0x80 (50%%) then 0xFF (100%%), commands %s; confirm the dim step by eye",
            (e1 || e2) ? "FAILED" : "accepted");
}

void disp_cycle_mode(void)
{
    if (!s_up) return;
    s_mode = (s_mode + 1) % 3;
    apply_mode();
    res_set(R_DISP, ST_INFO, "mode -> %s, touch map %s", MODE_NAMES[s_mode], disp_touch_map_name());
    disp_test_pattern();
}

const char *disp_mode_name(void) { return MODE_NAMES[s_mode]; }

void disp_cycle_touch_map(void)
{
    s_tmap = (s_tmap + 1) & 7;
    printf("touch map -> %s\n", disp_touch_map_name());
}

const char *disp_touch_map_name(void)
{
    static char b[40];
    snprintf(b, sizeof b, "swap=%d mirror_x=%d mirror_y=%d", s_tmap & 1, (s_tmap >> 1) & 1, (s_tmap >> 2) & 1);
    return b;
}

void disp_map_touch(uint16_t rx, uint16_t ry, int *lx, int *ly)
{
    int x = rx, y = ry;
    if (s_tmap & 2) x = PANEL_W - 1 - x;
    if (s_tmap & 4) y = PANEL_H - 1 - y;
    if (s_tmap & 1) { *lx = y; *ly = x; } else { *lx = x; *ly = y; }
    if (*lx < 0) *lx = 0;
    if (*ly < 0) *ly = 0;
    if (*lx > LAND_W - 1) *lx = LAND_W - 1;
    if (*ly > LAND_H - 1) *ly = LAND_H - 1;
}

void disp_dot(int lx, int ly, int radius)
{
    if (!s_up) return;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dx * dx + dy * dy <= radius * radius) fb_rect(lx + dx, ly + dy, 1, 1, RGB(255, 255, 255));
    push_land_rect(lx - radius, ly - radius, 2 * radius + 1, 2 * radius + 1);
}
