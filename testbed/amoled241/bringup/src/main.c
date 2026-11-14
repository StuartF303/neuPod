// SPDX-License-Identifier: MPL-2.0
// Bring-up probe for the Waveshare RP2350-Touch-AMOLED-2.41 (RP2350B, 16 MB flash, 2 MB PSRAM, RM690B0 450x600 AMOLED,
// FT6336 touch, PCF85063 RTC, QMI8658 IMU, microSD, soft-latched battery power).
// Dev-only capability test: prints PASS/FAIL for each block over USB CDC and UART0. Nothing here ever writes to the SD card.
// Every wait has a timeout; there is deliberately no watchdog (a watchdog reset would float BAT_Control and drop power on battery).

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/sync.h"

#include "DEV_Config.h"
#include "AMOLED_2in41.h"
#include "FT6336U.h"
#include "fonts.h"
#include "psram_tool.h"
#include "qspi_pio.h"

// ---- Pins (schematic + Waveshare demo) ----
#define PIN_TE        16
#define PIN_SD_SCK    18
#define PIN_SD_MOSI   19
#define PIN_SD_MISO   20
#define PIN_SD_CS     23
#define PIN_KEY_BAT   BAT_KEY      // 24, active low, pull-up
#define PIN_BAT_HOLD  BAT_Control  // 25, drive high to keep the soft latch
#define PIN_BAT_ADC   BAT_ADC      // 40 = ADC0 on the RP2350B
#define PIN_TP_INT    Touch_INT_PIN
#define PIN_TP_RST    Touch_RST_PIN

#define PW 450  // panel native (portrait) size
#define PH 600
#define LW 600  // landscape view
#define LH 450

#define PSRAM_CACHED   0x11000000u  // XIP window, QMI CS1, cached
#define PSRAM_UNCACHED 0x15000000u  // same chip, no cache: every access really goes to the PSRAM
#define PSRAM_TEST_BYTES (2u * 1024 * 1024)

#define PWR_HOLD_MS 3000

// ---- Result bookkeeping ----
#define MAX_RES 12
static struct {
    const char *name;
    bool done, ok;
    char msg[112];
} g_res[MAX_RES];
static int g_nres;

static void report(const char *name, bool ok, const char *fmt, ...) {
    char msg[112];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", name, msg);
    for (int i = 0; i < g_nres; i++) {
        if (g_res[i].name == name || strcmp(g_res[i].name, name) == 0) {
            g_res[i].ok = ok;
            snprintf(g_res[i].msg, sizeof g_res[i].msg, "%s", msg);
            return;
        }
    }
    if (g_nres < MAX_RES) {
        g_res[g_nres].name = name;
        g_res[g_nres].done = true;
        g_res[g_nres].ok = ok;
        snprintf(g_res[g_nres].msg, sizeof g_res[g_nres].msg, "%s", msg);
        g_nres++;
    }
}

static void print_summary(void) {
    printf("---- SUMMARY ----\n");
    for (int i = 0; i < g_nres; i++) {
        printf("  [%s] %-10s %s\n", g_res[i].ok ? "PASS" : "FAIL", g_res[i].name, g_res[i].msg);
    }
    printf("-----------------\n");
}

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }

// ---- I2C helpers (all with timeouts) ----
static bool i2c_rd(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_timeout_us(i2c1, addr, &reg, 1, true, 3000) != 1) {
        return false;
    }
    return i2c_read_timeout_us(i2c1, addr, buf, n, false, 3000 + (uint)n * 300) == (int)n;
}

// ---- Framebuffer (portrait order, RGB565 byte-swapped for the panel), landscape drawing on top ----
static uint16_t *g_fb;     // in PSRAM, 450x600x2 = 540000 bytes; NULL if PSRAM failed

#define C565(r, g, b) ((uint16_t)__builtin_bswap16((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))))

// Landscape (lx, ly) -> portrait (px, py): px = 449 - ly, py = lx.
static void port_fill(int px, int py, int w, int h, uint16_t c) {
    if (g_fb == NULL) return;
    if (px < 0) { w += px; px = 0; }
    if (py < 0) { h += py; py = 0; }
    if (px + w > PW) w = PW - px;
    if (py + h > PH) h = PH - py;
    for (int y = 0; y < h; y++) {
        uint16_t *row = g_fb + (py + y) * PW + px;
        for (int x = 0; x < w; x++) row[x] = c;
    }
}

static void land_fill(int lx, int ly, int w, int h, uint16_t c) {
    // landscape rect -> portrait rect (width h, height w)
    port_fill(PW - 1 - (ly + h - 1), lx, h, w, c);
}

static void land_text(int lx, int ly, const char *s, const sFONT *f, int scale, uint16_t c) {
    int bpr = (f->Width + 7) / 8;
    for (; *s; s++, lx += (int)f->Width * scale) {
        if (*s < ' ' || *s > '~') continue;
        const uint8_t *g = f->table + (*s - ' ') * f->Height * bpr;
        for (int row = 0; row < f->Height; row++) {
            for (int col = 0; col < f->Width; col++) {
                if (g[row * bpr + col / 8] & (0x80 >> (col % 8))) {
                    land_fill(lx + col * scale, ly + row * scale, scale, scale, c);
                }
            }
        }
    }
}

// ---- Display push with timeouts ----
// Pixel (x, y) of the source is org[(y - orgy) * PW + x]. The window is widened to even coordinates (the controller wants that).
static bool push_port(int x0, int y0, int x1, int y1, const uint16_t *org, int orgy) {
    x0 &= ~1; y0 &= ~1;
    x1 = (x1 + 1) & ~1; y1 = (y1 + 1) & ~1;
    if (x1 > PW) x1 = PW;
    if (y1 > PH) y1 = PH;
    if (x1 <= x0 || y1 <= y0) return true;

    AMOLED_2IN41_SetWindows((uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1);
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);
    channel_config_set_dreq(&c, pio_get_dreq(qspi.pio, qspi.sm, true));

    bool ok = true;
    int rows_per_dma = (x0 == 0 && x1 == PW) ? (y1 - y0) : 1;  // full-width windows are contiguous: one transfer
    for (int y = y0; y < y1 && ok; y += rows_per_dma) {
        const uint16_t *src = org + (size_t)(y - orgy) * PW + x0;
        dma_channel_configure(dma_tx, &c, &qspi.pio->txf[qspi.sm], src, (uint32_t)(x1 - x0) * 2 * rows_per_dma, true);
        absolute_time_t until = make_timeout_time_ms(500);
        while (dma_channel_is_busy(dma_tx)) {
            if (time_reached(until)) {
                dma_channel_abort(dma_tx);
                ok = false;
                break;
            }
        }
    }
    // The DMA is done when the last byte is in the PIO FIFO, not when it has been clocked out.
    absolute_time_t until = make_timeout_time_ms(20);
    while (!pio_sm_is_tx_fifo_empty(qspi.pio, qspi.sm) && !time_reached(until)) {
    }
    sleep_us(2);
    QSPI_Deselect(qspi);
    return ok;
}

static bool push_all(void) { return push_port(0, 0, PW, PH, g_fb, 0); }

static void lcd_cmd1(uint8_t cmd, uint8_t val) {
    QSPI_Select(qspi);
    QSPI_REGISTER_Write(qspi, cmd);
    QSPI_DATA_Write(qspi, val);
    QSPI_Deselect(qspi);
}

// ---- Step 1: chip / board ----
static void step_chip(void) {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);

    uint8_t tx[4] = {0x9F, 0, 0, 0}, rx[4] = {0};
    uint32_t irq = save_and_disable_interrupts();
    flash_do_cmd(tx, rx, 4);
    restore_interrupts(irq);
    uint32_t cap = rx[3] >= 10 && rx[3] <= 30 ? (1u << rx[3]) : 0;

    printf("\n==== neuPod AMOLED 2.41 bring-up probe (built %s %s) ====\n", __DATE__, __TIME__);
    printf("board : waveshare_rp2350_touch_amoled_2.41, pico-sdk %s\n", PICO_SDK_VERSION_STRING);
    printf("chip  : RP2350, rom chip version %u, %d bank0 GPIOs (48 = QFN80 'B'), PICO_RP2350A=%d\n", rp2350_chip_version(),
           NUM_BANK0_GPIOS, PICO_RP2350A);
    printf("id    : %02x%02x%02x%02x%02x%02x%02x%02x   sys clock %lu kHz\n", id.id[0], id.id[1], id.id[2], id.id[3], id.id[4],
           id.id[5], id.id[6], id.id[7], (unsigned long)(clock_get_hz(clk_sys) / 1000));
    printf("flash : JEDEC %02x %02x %02x -> %lu MB (build assumes %u MB)\n", rx[1], rx[2], rx[3],
           (unsigned long)(cap >> 20), PICO_FLASH_SIZE_BYTES >> 20);
    printf("power : GPIO%d (BAT_Control) driven high at start of main()\n", PIN_BAT_HOLD);
    report("chip", NUM_BANK0_GPIOS == 48 && cap == PICO_FLASH_SIZE_BYTES, "RP2350B (%d GPIOs), flash %lu MB",
           NUM_BANK0_GPIOS, (unsigned long)(cap >> 20));
}

// ---- Step 2: PSRAM ----
static inline uint32_t pat(uint32_t i) { return i * 2654435761u ^ 0xA5A5A5A5u; }

static uint32_t verify(const volatile uint32_t *p, uint32_t n, uint32_t inv, uint32_t *first_bad) {
    uint32_t errs = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] != (pat(i) ^ inv)) {
            if (errs == 0) *first_bad = i * 4;
            errs++;
        }
    }
    return errs;
}

static uint32_t mbps_x10(uint32_t bytes, uint64_t us) { return us ? (uint32_t)((uint64_t)bytes * 10 / us) : 0; }  // bytes/us = MB/s

static void step_psram(void) {
    size_t sz = rp_setup_psram(RP2350_XIP_CSI_PIN);
    printf("psram : CS1 = GPIO%d, KGD=0x%02x (0x5D = AP Memory) EID=0x%02x, driver-reported size %lu MB\n", RP2350_XIP_CSI_PIN,
           rp_psram_kgd, rp_psram_eid, (unsigned long)(sz >> 20));
    if (sz == 0) {
        report("psram", false, "not detected (KGD=0x%02x EID=0x%02x)", rp_psram_kgd, rp_psram_eid);
        return;
    }
    const uint32_t n = PSRAM_TEST_BYTES / 4;
    volatile uint32_t *u = (volatile uint32_t *)PSRAM_UNCACHED;
    volatile uint32_t *k = (volatile uint32_t *)PSRAM_CACHED;
    uint32_t bad = 0, errs = 0;

    // A: uncached write + read-back (every access hits the chip)
    uint64_t t0 = time_us_64();
    for (uint32_t i = 0; i < n; i++) u[i] = pat(i);
    uint64_t t1 = time_us_64();
    errs += verify(u, n, 0, &bad);
    uint64_t t2 = time_us_64();
    // B: inverted pattern through the cache, verify through the cache, then through the chip
    uint64_t t3 = time_us_64();
    for (uint32_t i = 0; i < n; i++) k[i] = ~pat(i);
    uint64_t t4 = time_us_64();
    errs += verify(k, n, 0xFFFFFFFFu, &bad);
    uint64_t t5 = time_us_64();
    errs += verify(u, n, 0xFFFFFFFFu, &bad);

    printf("psram : 2 MB pattern test, %lu word errors%s\n", (unsigned long)errs, errs ? " (first at +0x" : "");
    if (errs) printf("        %06lx)\n", (unsigned long)bad);
    printf("psram : write %lu.%lu MB/s uncached, %lu.%lu MB/s cached; read %lu.%lu MB/s uncached, %lu.%lu MB/s cached\n",
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t1 - t0) / 10), (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t1 - t0) % 10),
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t4 - t3) / 10), (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t4 - t3) % 10),
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t2 - t1) / 10), (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t2 - t1) % 10),
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t5 - t4) / 10), (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t5 - t4) % 10));

    // Capacity by address aliasing: a chip of N bytes ignores address bits above N, so offset N reads back what was
    // written at offset 0. The window is 16 MB; the smallest aliasing offset is the real size.
    uint32_t real_mb = 0;
    for (uint32_t off_mb = 2; off_mb <= 8 && real_mb == 0; off_mb *= 2) {
        u[0] = 0;
        u[(off_mb << 20) / 4] = 0x600DF00D;
        if (u[0] == 0x600DF00D) real_mb = off_mb;
    }
    if (real_mb) {
        printf("psram : address aliasing at +%lu MB -> real capacity %lu MB\n", (unsigned long)real_mb, (unsigned long)real_mb);
    } else {
        printf("psram : no aliasing up to +8 MB -> capacity >= 8 MB\n");
    }

    uint32_t mid = (errs == 0 && g_fb == NULL) ? 1 : 0;
    if (mid) g_fb = (uint16_t *)PSRAM_CACHED;  // framebuffer at the start of PSRAM (540000 bytes, fits in 2 MB)
    report("psram", errs == 0, "KGD 0x%02x EID 0x%02x, %lu MB (%s), %lu errors, rd %lu.%lu / wr %lu.%lu MB/s (cached)", rp_psram_kgd,
           rp_psram_eid, (unsigned long)(real_mb ? real_mb : (sz >> 20)), real_mb ? "aliasing" : "driver",
           (unsigned long)errs, (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t5 - t4) / 10),
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t5 - t4) % 10), (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t4 - t3) / 10),
           (unsigned long)(mbps_x10(PSRAM_TEST_BYTES, t4 - t3) % 10));
}

// ---- Step 3: display ----
static uint16_t g_sram_strip[PW * 100];  // 90 KB: a full 540 KB frame does not fit in the 520 KB of SRAM

static void draw_test_card(void) {
    static const uint8_t bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                       {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0}};
    for (int i = 0; i < 8; i++) {
        land_fill(i * 75, 0, 75, 300, C565(bars[i][0], bars[i][1], bars[i][2]));
    }
    for (int x = 0; x < LW; x++) {
        int g = x * 255 / (LW - 1);
        land_fill(x, 300, 1, 60, C565(g, g, g));
    }
    land_fill(0, 360, LW, 90, C565(0, 0, 0));
    land_text(20, 366, "neuPod 600x450", &Font24, 2, C565(255, 255, 255));
    land_text(10, 420, "touch the screen", &Font16, 1, C565(160, 160, 160));
    uint16_t o = C565(255, 128, 0);  // 1-px border, a colour that is not in the bars
    land_fill(0, 0, LW, 1, o);
    land_fill(0, LH - 1, LW, 1, o);
    land_fill(0, 0, 1, LH, o);
    land_fill(LW - 1, 0, 1, LH, o);
}

static uint32_t te_edges_100ms(void) {
    uint32_t edges = 0;
    bool last = gpio_get(PIN_TE);
    absolute_time_t until = make_timeout_time_ms(100);
    while (!time_reached(until)) {
        bool v = gpio_get(PIN_TE);
        if (v != last) { edges++; last = v; }
    }
    return edges;
}

static void step_display(void) {
    // QSPI_GPIO_Init also drives PWR_EN (GPIO17) high. Schematic says GPIO17 = PWR_EN; the C demo disabled it.
    QSPI_GPIO_Init(qspi);
    QSPI_PIO_Init(qspi);
    QSPI_4Wrie_Mode(&qspi);
    gpio_init(PIN_TE);
    gpio_set_dir(PIN_TE, GPIO_IN);
    AMOLED_2IN41_Init();
    printf("lcd   : RM690B0 init sequence sent (write-only bus: no read-back). PWR_EN GPIO%d high.\n", PIN_PWR_EN);
    uint32_t te = te_edges_100ms();
    printf("lcd   : TE (GPIO%d) edges in 100 ms: %lu (~12 for 60 Hz; TE was enabled by the init)\n", PIN_TE, (unsigned long)te);

    if (g_fb == NULL) {
        report("display", false, "no PSRAM framebuffer; showing solid red/green/blue only, TE edges %lu", (unsigned long)te);
        AMOLED_2IN41_Clear(0xF800);
        sleep_ms(500);
        AMOLED_2IN41_Clear(0x07E0);
        sleep_ms(500);
        AMOLED_2IN41_Clear(0x001F);
        return;
    }

    draw_test_card();
    bool ok = push_all();

    // Frame push timing: PSRAM (cached) source, whole 450x600 frame, 10 frames.
    const int N = 10;
    uint64_t t0 = time_us_64();
    for (int i = 0; i < N && ok; i++) ok = push_all();
    uint64_t us_psram = (time_us_64() - t0) / N;

    // SRAM source: 450x100 strips x6 = the same pixel count as a frame (a full frame cannot live in SRAM).
    for (int i = 0; i < PW * 100; i++) g_sram_strip[i] = C565(0, 64, 128);
    t0 = time_us_64();
    for (int f = 0; f < N && ok; f++) {
        for (int s = 0; s < 6 && ok; s++) ok = push_port(0, s * 100, PW, s * 100 + 100, g_sram_strip, s * 100);
    }
    uint64_t us_sram = (time_us_64() - t0) / N;
    // Put the test card back (the strip test painted over it).
    ok = push_all() && ok;

    printf("lcd   : full-frame push 450x600 from PSRAM: %lu.%03lu ms = %lu.%lu fps\n", (unsigned long)(us_psram / 1000),
           (unsigned long)(us_psram % 1000), (unsigned long)(1000000 / (us_psram ? us_psram : 1)),
           (unsigned long)((10000000 / (us_psram ? us_psram : 1)) % 10));
    printf("lcd   : same pixel count from SRAM (6 strips of 450x100): %lu.%03lu ms = %lu.%lu fps (full frame does not fit SRAM)\n",
           (unsigned long)(us_sram / 1000), (unsigned long)(us_sram % 1000), (unsigned long)(1000000 / (us_sram ? us_sram : 1)),
           (unsigned long)((10000000 / (us_sram ? us_sram : 1)) % 10));

    printf("lcd   : brightness 50%% (0x51 = 0x7F) ...\n");
    AMOLED_2IN41_SetBrightness(50);
    sleep_ms(700);
    printf("lcd   : brightness 100%% (0x51 = 0xFF)\n");
    AMOLED_2IN41_SetBrightness(100);

    printf("lcd   : orientation: landscape is done in software (framebuffer stored rotated, so no extra cost). Hardware rotation\n"
           "        via MADCTL (0x36) is untested: send 'm' on the console to cycle MADCTL values and watch the screen.\n");
    report("display", ok && te > 0, "TE edges %lu, PSRAM push %lu.%lu ms (%lu fps), SRAM-source %lu.%lu ms; look at the panel for the test card",
           (unsigned long)te, (unsigned long)(us_psram / 1000), (unsigned long)((us_psram % 1000) / 100),
           (unsigned long)(1000000 / (us_psram ? us_psram : 1)), (unsigned long)(us_sram / 1000),
           (unsigned long)((us_sram % 1000) / 100));
}

// ---- Step 4: touch ----
static bool g_touch_ok;

static void step_touch_init(void) {
    gpio_init(PIN_TP_INT);
    gpio_set_dir(PIN_TP_INT, GPIO_IN);
    gpio_pull_up(PIN_TP_INT);
    gpio_put(PIN_TP_RST, 1);
    sleep_ms(10);
    gpio_put(PIN_TP_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_TP_RST, 1);
    sleep_ms(300);

    uint8_t chip = 0, vendor = 0, fw = 0, lib[2] = {0, 0}, gmode = 0;
    bool ok = i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_CHIP_ID, &chip, 1);
    if (!ok) {
        report("touch", false, "no ACK from 0x%02x (reset GPIO%d, INT GPIO%d)", FT6336U_I2C_ADDR, PIN_TP_RST, PIN_TP_INT);
        return;
    }
    i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_FOCALTECH_ID, &vendor, 1);
    i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_FIRMARE_ID, &fw, 1);
    i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_LIBRARY_VERSION_H, lib, 2);
    i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_G_MODE, &gmode, 1);
    printf("touch : chip ID 0x%02x (FT6336U = 0x64), vendor ID 0x%02x (FocalTech = 0x11), firmware 0x%02x, lib 0x%02x%02x, G_MODE %u, INT=%d\n",
           chip, vendor, fw, lib[0], lib[1], gmode, gpio_get(PIN_TP_INT));
    g_touch_ok = true;
    report("touch", chip == 0x64, "chip 0x%02x vendor 0x%02x fw 0x%02x; reporting points below (landscape x,y)", chip, vendor, fw);
}

static void touch_poll(void) {
    static bool down;
    static uint32_t last_print, last_draw, fails;
    static int last_px = -1, last_py = -1;
    if (!g_touch_ok) return;

    uint8_t b[9];
    if (!i2c_rd(FT6336U_I2C_ADDR, FT6336U_ADDR_TD_STATUS, b, sizeof b)) {
        if (++fails == 20) printf("touch : I2C read failing (20 in a row)\n");
        return;
    }
    fails = 0;
    int n = b[0] & 0x0F;
    if (n == 0 || n > 2) {
        if (down) {
            down = false;
            printf("touch : up\n");
        }
        return;
    }
    int px = ((b[1] & 0x0F) << 8) | b[2];
    int py = ((b[3] & 0x0F) << 8) | b[4];
    int ev = b[1] >> 6;  // 0 down, 1 up, 2 contact
    if (px >= PW) px = PW - 1;
    if (py >= PH) py = PH - 1;
    int lx = py, ly = PW - 1 - px;
    uint32_t now = ms_now();
    if (!down || (now - last_print >= 100 && (px != last_px || py != last_py))) {
        char extra[48] = "";
        if (n == 2) {
            int p2x = ((b[7] & 0x0F) << 8) | b[8];
            snprintf(extra, sizeof extra, "  (2nd finger raw x=%d)", p2x);
        }
        printf("touch : %s n=%d land x=%d y=%d (raw portrait x=%d y=%d, ev=%d)%s\n", down ? "move" : "down", n, lx, ly, px, py, ev,
               extra);
        last_print = now;
    }
    down = true;
    if ((px != last_px || py != last_py) && g_fb != NULL && now - last_draw >= 20) {
        last_px = px;
        last_py = py;
        last_draw = now;
        land_fill(lx - 6, ly - 6, 13, 13, C565(255, 0, 0));
        push_port(px - 7, py - 7, px + 8, py + 8, g_fb, 0);
        char s[40];
        snprintf(s, sizeof s, "x=%3d y=%3d   ", lx, ly);
        land_fill(0, 415, LW, 30, C565(0, 0, 0));
        land_text(10, 420, s, &Font16, 1, C565(255, 255, 255));
        push_port(PW - 1 - 444, 0, PW - 1 - 415 + 1, PH, g_fb, 0);
    }
}

// ---- Step 5: I2C scan, IMU, RTC ----
static void step_i2c(void) {
    bool found[128] = {0};
    printf("i2c1  : scan (SDA GPIO6, SCL GPIO7):");
    int n = 0;
    for (int a = 0x08; a < 0x78; a++) {
        uint8_t d;
        if (i2c_read_timeout_us(i2c1, (uint8_t)a, &d, 1, false, 2000) >= 0) {
            found[a] = true;
            printf(" 0x%02x", a);
            n++;
        }
    }
    printf(" (%d found)\n", n);
    bool imu_addr = found[0x6B] || found[0x6A];
    report("i2c-scan", found[0x38] && found[0x51] && imu_addr, "%d found; FT6336 0x38 %s, PCF85063 0x51 %s, QMI8658 %s", n,
           found[0x38] ? "yes" : "NO", found[0x51] ? "yes" : "NO", found[0x6B] ? "0x6B" : found[0x6A] ? "0x6A" : "NO");

    uint8_t imu = found[0x6B] ? 0x6B : 0x6A;
    uint8_t who = 0, rev = 0;
    bool ok = imu_addr && i2c_rd(imu, 0x00, &who, 1) && i2c_rd(imu, 0x01, &rev, 1);
    report("imu", ok && who == 0x05, ok ? "QMI8658 @0x%02x WHO_AM_I 0x%02x (expect 0x05), revision 0x%02x" : "no response @0x%02x", imu,
           who, rev);

    uint8_t t[7];
    ok = found[0x51] && i2c_rd(0x51, 0x04, t, sizeof t);
    if (ok) {
        #define BCD(v) (((v) >> 4) * 10 + ((v) & 0x0F))
        int sec = BCD(t[0] & 0x7F), min = BCD(t[1] & 0x7F), hr = BCD(t[2] & 0x3F), day = BCD(t[3] & 0x3F);
        int mon = BCD(t[5] & 0x1F), yr = 2000 + BCD(t[6]);
        bool os = t[0] & 0x80;  // oscillator-stop flag: time not valid (never set / battery gone)
        bool sane = sec < 60 && min < 60 && hr < 24 && day >= 1 && day <= 31 && mon >= 1 && mon <= 12;
        report("rtc", sane, "PCF85063 %04d-%02d-%02d %02d:%02d:%02d%s", yr, mon, day, hr, min, sec,
               os ? " (OS flag set: clock never set / lost power)" : "");
    } else {
        report("rtc", false, "PCF85063 @0x51 not readable");
    }
}

// ---- Step 6: battery ----
static uint32_t bat_mv(uint32_t *raw_out) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += adc_read();
    uint32_t raw = sum / 16;
    if (raw_out) *raw_out = raw;
    return raw * 3300 * 3 / 4096;  // 3.3 V reference, 200k/100k divider -> x3
}

static void step_battery(void) {
    uint32_t raw;
    uint32_t mv = bat_mv(&raw);
    printf("batt  : GPIO%d (ADC0): raw %lu, Vbat %lu mV, KEY_BAT (GPIO%d) %s\n", PIN_BAT_ADC, (unsigned long)raw, (unsigned long)mv,
           PIN_KEY_BAT, gpio_get(PIN_KEY_BAT) ? "released" : "PRESSED");
    report("battery", mv >= 2500 && mv <= 4600, "%lu mV (raw %lu)%s", (unsigned long)mv, (unsigned long)raw,
           mv < 2500 ? " - no battery seen (fine if USB-only)" : "");
}

// ---- Step 7: microSD over SPI0, read-only ----
static uint8_t sd_x(uint8_t b) {
    uint8_t r;
    spi_write_read_blocking(spi0, &b, &r, 1);
    return r;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    sd_x(0xFF);
    sd_x(0x40 | cmd);
    sd_x(arg >> 24);
    sd_x(arg >> 16);
    sd_x(arg >> 8);
    sd_x(arg);
    sd_x(crc);
    uint8_t r = 0xFF;
    for (int i = 0; i < 10; i++) {
        r = sd_x(0xFF);
        if (!(r & 0x80)) break;
    }
    return r;
}

static void sd_end(void) {
    gpio_put(PIN_SD_CS, 1);
    sd_x(0xFF);
}

static void step_sd(void) {
    spi_init(spi0, 400 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
    gpio_pull_up(PIN_SD_MISO);
    gpio_init(PIN_SD_CS);
    gpio_set_dir(PIN_SD_CS, GPIO_OUT);
    gpio_put(PIN_SD_CS, 1);

    for (int i = 0; i < 10; i++) sd_x(0xFF);  // 80 clocks with CS high
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && r != 0x01; i++) {
        gpio_put(PIN_SD_CS, 0);
        r = sd_cmd(0, 0, 0x95);
        sd_end();
    }
    if (r != 0x01) {
        report("sd", false, "CMD0 gave 0x%02x (no card, or card not seated)", r);
        return;
    }
    bool v2 = false;
    gpio_put(PIN_SD_CS, 0);
    r = sd_cmd(8, 0x1AA, 0x87);
    if (r == 0x01) {
        uint8_t e[4];
        for (int i = 0; i < 4; i++) e[i] = sd_x(0xFF);
        v2 = (e[2] & 0x0F) == 0x01 && e[3] == 0xAA;
    }
    sd_end();
    printf("sd    : CMD0 ok, CMD8 r1=0x%02x -> %s\n", r, v2 ? "SD v2" : "v1 or MMC");

    absolute_time_t until = make_timeout_time_ms(1500);
    r = 0xFF;
    while (!time_reached(until)) {
        gpio_put(PIN_SD_CS, 0);
        sd_cmd(55, 0, 0x65);
        r = sd_cmd(41, v2 ? 0x40000000u : 0, 0x77);
        sd_end();
        if (r == 0x00) break;
        sleep_ms(10);
    }
    if (r != 0x00) {
        report("sd", false, "ACMD41 did not leave idle (r1=0x%02x) within 1.5 s", r);
        return;
    }
    bool hc = false;
    if (v2) {
        gpio_put(PIN_SD_CS, 0);
        if (sd_cmd(58, 0, 0xFD) == 0x00) {
            uint8_t o0 = sd_x(0xFF);
            for (int i = 0; i < 3; i++) sd_x(0xFF);
            hc = o0 & 0x40;
        }
        sd_end();
    }
    uint actual = spi_set_baudrate(spi0, 12500 * 1000);
    printf("sd    : initialised (%s), SPI raised from 400 kHz to %u Hz (asked 12.5 MHz)\n", hc ? "SDHC/SDXC" : "SDSC", actual);

    uint8_t csd[16];
    bool ok = false;
    gpio_put(PIN_SD_CS, 0);
    r = sd_cmd(9, 0, 0xAF);
    if (r == 0x00) {
        absolute_time_t tok = make_timeout_time_ms(200);
        uint8_t t = 0xFF;
        while (t != 0xFE && !time_reached(tok)) t = sd_x(0xFF);
        if (t == 0xFE) {
            for (int i = 0; i < 16; i++) csd[i] = sd_x(0xFF);
            sd_x(0xFF);
            sd_x(0xFF);
            ok = true;
        }
    }
    sd_end();
    if (!ok) {
        report("sd", false, "CMD9 (read CSD) failed, r1=0x%02x", r);
        return;
    }
    uint64_t bytes;
    if ((csd[0] >> 6) == 1) {
        uint32_t cs = ((uint32_t)(csd[7] & 0x3F) << 16) | (csd[8] << 8) | csd[9];
        bytes = (uint64_t)(cs + 1) * 512 * 1024;
    } else {
        uint32_t cs = ((csd[6] & 3) << 10) | (csd[7] << 2) | (csd[8] >> 6);
        uint32_t mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
        bytes = ((uint64_t)(cs + 1) << (mult + 2)) << (csd[5] & 0x0F);
    }
    printf("sd    : CSD ");
    for (int i = 0; i < 16; i++) printf("%02x", csd[i]);
    printf(" (v%d)\n", (csd[0] >> 6) + 1);
    report("sd", true, "%s, %lu MB (%lu.%lu GB), SPI %u Hz, read-only", hc ? "SDHC/SDXC" : "SDSC",
           (unsigned long)(bytes >> 20), (unsigned long)(bytes / 1000000000u), (unsigned long)((bytes / 100000000u) % 10), actual);
}

// ---- Console commands ----
static void console_poll(void) {
    static const uint8_t madctl[] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0};
    static int idx;
    int ch = getchar_timeout_us(0);
    if (ch == 'm') {
        idx = (idx + 1) % (int)(sizeof madctl);
        lcd_cmd1(0x36, madctl[idx]);
        printf("lcd   : MADCTL 0x36 = 0x%02x; re-pushing the (portrait-order) frame - note what the panel does\n", madctl[idx]);
        if (g_fb) push_all();
    } else if (ch == 's') {
        print_summary();
    }
}

int main(void) {
    // Keep the soft latch closed before anything else. gpio_init() leaves the pin as an input, so latch the level
    // first and only then make it an output: no low glitch on the power switch.
    gpio_init(PIN_BAT_HOLD);
    gpio_put(PIN_BAT_HOLD, 1);
    gpio_set_dir(PIN_BAT_HOLD, GPIO_OUT);
    gpio_init(PIN_KEY_BAT);
    gpio_pull_up(PIN_KEY_BAT);
    gpio_set_dir(PIN_KEY_BAT, GPIO_IN);

    // 150 MHz like the Waveshare demo; set before stdio so the UART baud rate is computed for the final clock.
    set_sys_clock_khz(PLL_SYS_KHZ, true);
    stdio_init_all();

    // Wait for a USB terminal (up to 6 s) so the first lines are not lost; carry on regardless.
    absolute_time_t until = make_timeout_time_ms(6000);
    while (!stdio_usb_connected() && !time_reached(until)) sleep_ms(20);
    sleep_ms(200);

    // Peripherals the Waveshare helpers expect (what DEV_Module_Init does, minus its second stdio init).
    gpio_init(PIN_TP_RST);
    gpio_set_dir(PIN_TP_RST, GPIO_OUT);
    gpio_put(PIN_TP_RST, 1);
    adc_init();
    adc_gpio_init(PIN_BAT_ADC);
    adc_select_input(BAT_CHANNEL);
    dma_tx = dma_claim_unused_channel(true);
    c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(DEV_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DEV_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DEV_SDA_PIN);
    gpio_pull_up(DEV_SCL_PIN);

    step_chip();
    step_psram();
    step_display();
    step_touch_init();
    step_i2c();
    step_battery();
    step_sd();
    print_summary();
    printf("Running: touch points, battery every 2 s. Console: 'm' = cycle MADCTL, 's' = summary. Hold Power %d s to switch off.\n",
           PWR_HOLD_MS / 1000);

    bool key_armed = false;  // only count a hold once the key has been seen released (it may be what powered us on)
    uint32_t press_start = 0, next_bat = ms_now() + 2000, next_sum = ms_now() + 20000;
    bool off_done = false;
    for (;;) {
        touch_poll();
        console_poll();

        uint32_t now = ms_now();
        bool pressed = !gpio_get(PIN_KEY_BAT);
        if (!pressed) {
            key_armed = true;
            press_start = 0;
            off_done = false;
        } else if (key_armed && !off_done) {
            if (press_start == 0) press_start = now ? now : 1;
            if (now - press_start >= PWR_HOLD_MS) {
                off_done = true;
                printf("power : Power held %d s -> releasing BAT_Control (GPIO%d). On battery the board now switches off.\n",
                       PWR_HOLD_MS / 1000, PIN_BAT_HOLD);
                stdio_flush();
                sleep_ms(100);
                gpio_put(PIN_BAT_HOLD, 0);
                sleep_ms(1000);
                printf("power : still running, so USB is powering the board (or the key is still keeping the latch).\n");
                gpio_put(PIN_BAT_HOLD, 1);
            }
        }
        if ((int32_t)(now - next_bat) >= 0) {
            next_bat = now + 2000;
            uint32_t raw;
            uint32_t mv = bat_mv(&raw);
            printf("batt  : %lu mV (raw %lu), KEY_BAT %s\n", (unsigned long)mv, (unsigned long)raw, pressed ? "PRESSED" : "released");
        }
        if ((int32_t)(now - next_sum) >= 0) {
            next_sum = now + 20000;
            print_summary();  // repeated so a terminal opened late still sees the results
        }
        sleep_ms(5);
    }
}
