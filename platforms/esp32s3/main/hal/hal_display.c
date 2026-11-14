// SPDX-License-Identifier: MPL-2.0
/*
 * Display: the board's 450 x 600 RM690B0 AMOLED on QSPI, presented as 600 x 450 landscape (MADCTL 0x30; the esp_lcd SH8601 driver
 * the probe tried has no swap_xy, so this file talks to the panel through esp_lcd's SPI panel IO directly and needs no extra
 * component). The RM690B0 takes the SH8601 framing: opcode 0x02 + command in the address phase for commands, 0x32 + 0x2C00 for
 * pixel data (RAMWR); bring-up values are the probe's (testbed/esp32amoled241/bringup/main/display.c), written from the register
 * values the factory demo uses, no vendor source is copied.
 *
 * The controller wants windows that start on an even and end on an odd pixel. The UI (LVGL) hands over arbitrary partial-render
 * areas, so a full shadow of the panel's pixels is kept in PSRAM (540 KB, already in the panel's byte order): hal_display_blit()
 * writes the area into it and pushes the area widened to even edges from it, which is correct whatever the caller's alignment.
 * The push goes through two internal-RAM DMA strips (the panel cannot be fed from PSRAM directly at speed); a whole frame took 31 ms
 * on the bench.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal_display.h"

#define LCD_HOST SPI2_HOST
#define LCD_PCLK_HZ (40 * 1000 * 1000)
#define STRIP_PX 9600 /* one DMA strip: 16 landscape rows */
#define XFER_TIMEOUT_MS 500

#define CMD(c) ((0x02u << 24) | ((uint32_t)(c) << 8))
#define RAMWR ((0x32u << 24) | (0x2Cu << 8))

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;        /* LAND_W x LAND_H shadow, panel byte order (big-endian RGB565) */
static uint16_t *s_bounce[2]; /* internal, DMA capable */
static SemaphoreHandle_t s_done;
static bool s_up;
static uint8_t s_madctl = 0x30;
static bool s_inverted;
static uint8_t s_brightness;
static bool s_asleep;
// Threading: every display call comes from the UI task (init, blit, sleep, backlight), so there is no lock here.

/* The write window and the cursor in it (hal_display_set_window / blit). */
static uint16_t s_wx, s_wy, s_ww, s_wh, s_cx, s_cy;

/* RM690B0 bring-up: {cmd, data, length, delay ms}. The display is switched on (0x29) after the first frame is in. */
typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    uint8_t len;
    uint16_t delay_ms;
} init_cmd_t;

static const init_cmd_t INIT_CMDS[] = {
    {0xFE, (const uint8_t[]){0x20}, 1, 0}, /* user command set 1 */
    {0x26, (const uint8_t[]){0x0A}, 1, 0},
    {0x24, (const uint8_t[]){0x80}, 1, 0},
    {0xFE, (const uint8_t[]){0x00}, 1, 0}, /* back to the standard command set */
    {0x3A, (const uint8_t[]){0x55}, 1, 0}, /* 16 bit/pixel */
    {0xC2, (const uint8_t[]){0x00}, 1, 10},
    {0x35, NULL, 0, 0},                    /* tearing effect line on */
    {0x51, (const uint8_t[]){0x00}, 1, 10}, /* brightness 0 until the first frame is in */
    {0x11, NULL, 0, 80},                   /* sleep out */
};

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io;
    (void)ev;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t cmd(uint8_t c, const void *data, size_t len) { return esp_lcd_panel_io_tx_param(s_io, (int)CMD(c), data, len); }

static uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

/* Set the panel's RAM window (inclusive start, exclusive end, in landscape coordinates) and queue pixels for it. */
static esp_err_t send_rect(int x0, int y0, int x1, int y1, const uint16_t *px) {
    uint8_t col[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)((x1 - 1) >> 8), (uint8_t)(x1 - 1)};
    int ry0 = y0 + PANEL_RAM_OFFSET, ry1 = y1 + PANEL_RAM_OFFSET;
    uint8_t row[4] = {(uint8_t)(ry0 >> 8), (uint8_t)ry0, (uint8_t)((ry1 - 1) >> 8), (uint8_t)(ry1 - 1)};
    esp_err_t e = cmd(0x2A, col, 4);
    if (e == ESP_OK) {
        e = cmd(0x2B, row, 4);
    }
    if (e == ESP_OK) {
        e = esp_lcd_panel_io_tx_color(s_io, (int)RAMWR, px, (size_t)(x1 - x0) * (size_t)(y1 - y0) * 2u);
    }
    return e;
}

/* Push the shadow's rectangle [x0..x1] x [y0..y1] (inclusive, any alignment) to the panel, widened to even edges. */
static bool push_rect(int x0, int y0, int x1, int y1) {
    x0 &= ~1;
    y0 &= ~1;
    x1 |= 1;
    y1 |= 1;
    if (x1 > LAND_W - 1) {
        x1 = LAND_W - 1;
    }
    if (y1 > LAND_H - 1) {
        y1 = LAND_H - 1;
    }
    int pw = x1 - x0 + 1;
    int rows = (STRIP_PX / pw) & ~1;
    if (rows < 2) {
        return false;
    }
    while (xSemaphoreTake(s_done, 0) == pdTRUE) { /* drop stale completions */
    }
    int inflight = 0, buf = 0;
    bool ok = true;
    for (int y = y0; y <= y1 && ok; y += rows) {
        int n = (y1 - y + 1 < rows) ? (y1 - y + 1) : rows;
        if (inflight == 2) {
            ok = xSemaphoreTake(s_done, pdMS_TO_TICKS(XFER_TIMEOUT_MS)) == pdTRUE;
            inflight--;
        }
        for (int r = 0; r < n; r++) {
            memcpy(s_bounce[buf] + r * pw, s_fb + (size_t)(y + r) * LAND_W + x0, (size_t)pw * 2u);
        }
        if (send_rect(x0, y, x1 + 1, y + n, s_bounce[buf]) != ESP_OK) {
            ok = false;
        }
        inflight++;
        buf ^= 1;
    }
    while (inflight-- > 0) {
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(XFER_TIMEOUT_MS)) != pdTRUE) {
            ok = false;
        }
    }
    return ok;
}

static void release_buffers(void) {
    heap_caps_free(s_fb);
    s_fb = NULL;
    for (int i = 0; i < 2; i++) {
        heap_caps_free(s_bounce[i]);
        s_bounce[i] = NULL;
    }
    if (s_done != NULL) {
        vSemaphoreDelete(s_done);
        s_done = NULL;
    }
}

hal_status_t hal_display_init(void) {
    if (s_up) {
        return HAL_OK;
    }
    // The panel's power enable lives on the I2C expander: without it the panel is unpowered and every command would "succeed" into
    // nothing, so report it as a failed display rather than a working one.
    if (board_init() != ESP_OK) {
        printf("display: panel power enable (TCA9554) not available\n");
        return HAL_ENODEV;
    }
    s_done = xSemaphoreCreateCounting(16, 0);
    s_fb = heap_caps_aligned_alloc(64, (size_t)LAND_W * LAND_H * 2u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < 2; i++) {
        s_bounce[i] = heap_caps_aligned_alloc(64, STRIP_PX * 2u, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (s_done == NULL || s_fb == NULL || s_bounce[0] == NULL || s_bounce[1] == NULL) {
        printf("display: out of memory (shadow %p, strips %p/%p)\n", (void *)s_fb, (void *)s_bounce[0], (void *)s_bounce[1]);
        release_buffers();
        return HAL_ERR;
    }
    memset(s_fb, 0, (size_t)LAND_W * LAND_H * 2u);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_CLK,
        .data0_io_num = PIN_LCD_D0,
        .data1_io_num = PIN_LCD_D1,
        .data2_io_num = PIN_LCD_D2,
        .data3_io_num = PIN_LCD_D3,
        .max_transfer_sz = 32768,
    };
    esp_err_t e = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        printf("display: spi bus: %s\n", esp_err_to_name(e));
        release_buffers();
        return HAL_ERR;
    }
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);
    if (e != ESP_OK) {
        printf("display: panel io: %s\n", esp_err_to_name(e));
        spi_bus_free(LCD_HOST);
        release_buffers();
        return HAL_ERR;
    }

    // Hardware reset, then the bring-up sequence.
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t zero = 0x00, rgb565 = 0x55;
    e = cmd(0x11, NULL, 0); /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(100));
    if (e == ESP_OK) {
        e = cmd(0x36, &zero, 1);
    }
    if (e == ESP_OK) {
        e = cmd(0x3A, &rgb565, 1);
    }
    for (size_t i = 0; e == ESP_OK && i < sizeof INIT_CMDS / sizeof INIT_CMDS[0]; i++) {
        e = cmd(INIT_CMDS[i].cmd, INIT_CMDS[i].data, INIT_CMDS[i].len);
        if (INIT_CMDS[i].delay_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(INIT_CMDS[i].delay_ms));
        }
    }
    if (e == ESP_OK) {
        e = cmd(0x36, &s_madctl, 1); /* landscape */
    }
    if (e != ESP_OK) {
        printf("display: init sequence: %s\n", esp_err_to_name(e));
        return HAL_ERR;
    }
    s_up = true;
    s_brightness = 0;
    // Clear to black, then switch the panel on (the backlight call raises the brightness after the first real frame).
    if (!push_rect(0, 0, LAND_W - 1, LAND_H - 1)) {
        printf("display: first frame did not complete\n");
        s_up = false;
        return HAL_ETIMEOUT;
    }
    (void)cmd(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    printf("display: RM690B0 %dx%d landscape, QSPI %d MHz, shadow %u KB in PSRAM\n", LAND_W, LAND_H, LCD_PCLK_HZ / 1000000,
           (unsigned)((size_t)LAND_W * LAND_H * 2u / 1024u));
    return HAL_OK;
}

uint16_t hal_display_width(void) { return LAND_W; }
uint16_t hal_display_height(void) { return LAND_H; }
const char *hal_display_name(void) { return "RM690B0"; }

hal_status_t hal_display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!s_up) {
        return HAL_ENODEV;
    }
    if (w == 0 || h == 0 || (uint32_t)x + w > LAND_W || (uint32_t)y + h > LAND_H) {
        return HAL_EINVAL;
    }
    s_wx = x;
    s_wy = y;
    s_ww = w;
    s_wh = h;
    s_cx = s_cy = 0;
    return HAL_OK;
}

hal_status_t hal_display_blit(const hal_pixel_t *pixels, size_t count) {
    if (!s_up) {
        return HAL_ENODEV;
    }
    if (pixels == NULL || s_ww == 0) {
        return HAL_EINVAL;
    }
    if (count == 0) {
        return HAL_OK;
    }
    uint16_t first_row = s_cy, last_row = s_cy;
    size_t i = 0;
    while (i < count) {
        size_t run = (size_t)(s_ww - s_cx);
        if (run > count - i) {
            run = count - i;
        }
        uint16_t *dst = s_fb + (size_t)(s_wy + s_cy) * LAND_W + s_wx + s_cx;
        for (size_t k = 0; k < run; k++) {
            dst[k] = swap16(pixels[i + k]);
        }
        last_row = s_cy;
        i += run;
        s_cx = (uint16_t)(s_cx + run);
        if (s_cx == s_ww) { /* wrap to the window origin after the last row, as the controller does */
            s_cx = 0;
            s_cy = (uint16_t)((s_cy + 1) % s_wh);
        }
    }
    // The rows this call touched; if it wrapped past the end of the window, the whole window.
    bool wrapped = count > (size_t)s_ww * s_wh || last_row < first_row;
    int y0 = s_wy + (wrapped ? 0 : first_row), y1 = s_wy + (wrapped ? s_wh - 1 : last_row);
    return push_rect(s_wx, y0, s_wx + s_ww - 1, y1) ? HAL_OK : HAL_ETIMEOUT;
}

void hal_display_wait(void) {} /* blit completes its transfers before it returns */

void hal_display_present(void) {} /* the panel has its own frame memory */

hal_status_t hal_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, hal_pixel_t colour) {
    if (!s_up) {
        return HAL_ENODEV;
    }
    if (w == 0 || h == 0 || (uint32_t)x + w > LAND_W || (uint32_t)y + h > LAND_H) {
        return HAL_EINVAL;
    }
    uint16_t c = swap16(colour);
    for (uint16_t r = 0; r < h; r++) {
        uint16_t *row = s_fb + (size_t)(y + r) * LAND_W + x;
        for (uint16_t k = 0; k < w; k++) {
            row[k] = c;
        }
    }
    s_wx = x;
    s_wy = y;
    s_ww = w;
    s_wh = h;
    s_cx = s_cy = 0;
    return push_rect(x, y, x + w - 1, y + h - 1) ? HAL_OK : HAL_ETIMEOUT;
}

void hal_display_sleep(bool sleep) {
    if (!s_up || sleep == s_asleep) {
        return;
    }
    if (sleep) {
        (void)cmd(0x28, NULL, 0); /* DISPOFF */
        vTaskDelay(pdMS_TO_TICKS(10));
        (void)cmd(0x10, NULL, 0); /* SLPIN */
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        (void)cmd(0x11, NULL, 0); /* SLPOUT */
        vTaskDelay(pdMS_TO_TICKS(120));
        (void)cmd(0x29, NULL, 0); /* DISPON */
        vTaskDelay(pdMS_TO_TICKS(20));
        (void)cmd(0x51, &s_brightness, 1);
    }
    s_asleep = sleep;
}

void hal_display_service(void) {}

void hal_display_backlight(uint8_t level) {
    s_brightness = level;
    if (s_up && !s_asleep) {
        (void)cmd(0x51, &level, 1);
    }
}

void hal_display_set_madctl(uint8_t madctl) {
    s_madctl = madctl;
    if (s_up) {
        (void)cmd(0x36, &madctl, 1);
    }
}

uint8_t hal_display_get_madctl(void) { return s_madctl; }

void hal_display_set_inversion(bool on) {
    s_inverted = on;
    if (s_up) {
        (void)cmd(on ? 0x21 : 0x20, NULL, 0);
    }
}

bool hal_display_get_inversion(void) { return s_inverted; }
