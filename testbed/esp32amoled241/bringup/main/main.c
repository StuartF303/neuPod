// SPDX-License-Identifier: MPL-2.0
// neuPod bring-up probe for the Waveshare ESP32-S3-Touch-AMOLED-2.41 (dev-only capability testing).
// Every step reports PASS/FAIL with numbers; nothing here blocks without a timeout.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "periph.h"
#include "radio.h"
#include "results.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"

// ---------------- step 1: chip, flash, heap ----------------

static void step_chip(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t flash_sz = 0, jedec = 0;
    esp_flash_get_physical_size(NULL, &flash_sz);
    esp_flash_read_id(NULL, &jedec);
    size_t ps = esp_psram_get_size();
    bool ok = flash_sz == 16u * 1024 * 1024 && ps >= 7u * 1024 * 1024;
    res_set(R_BOOT, ok ? ST_PASS : ST_FAIL,
            "ESP32-S3 rev v%d.%d, %d cores, %s PSRAM; flash %u MB (JEDEC 0x%06X); PSRAM %u KB; free heap internal %u KB, PSRAM %u KB; reset reason %d",
            ci.revision / 100, ci.revision % 100, ci.cores, (ci.features & CHIP_FEATURE_EMB_PSRAM) ? "embedded" : "efuse flag clear for",
            (unsigned)(flash_sz >> 20), (unsigned)jedec, (unsigned)(ps >> 10),
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10),
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10), (int)esp_reset_reason());
}

// ---------------- step 2: PSRAM ----------------

static uint32_t pat(int pass, size_t i, const uint32_t *base)
{
    switch (pass) {
    case 0: return (uint32_t)(uintptr_t)(base + i);          // address in address: catches address-line faults
    case 1: return (uint32_t)i * 2654435761u ^ 0xA5A5A5A5u;  // pseudo-random
    default: return ~((uint32_t)i * 2654435761u ^ 0xA5A5A5A5u);
    }
}

static void step_psram(void)
{
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t sz = blk & ~(size_t)63;
    uint32_t *p = NULL;
    for (int tries = 0; tries < 8 && !p; tries++) {
        p = heap_caps_aligned_alloc(64, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) sz -= 64 * 1024;
    }
    if (!p) {
        res_set(R_PSRAM, ST_FAIL, "could not allocate (largest block %u KB)", (unsigned)(blk >> 10));
        return;
    }
    size_t words = sz / 4, errs = 0, chunk = 64 * 1024;
    int64_t wr_us = 0, rd_us = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < words; i += chunk) {
            size_t e = (i + chunk < words) ? i + chunk : words;
            int64_t t = esp_timer_get_time();
            for (size_t k = i; k < e; k++) p[k] = pat(pass, k, p);
            if (pass == 0) wr_us += esp_timer_get_time() - t;
            vTaskDelay(1);   // keep the idle task and watchdog fed
        }
        for (size_t i = 0; i < words; i += chunk) {
            size_t e = (i + chunk < words) ? i + chunk : words;
            int64_t t = esp_timer_get_time();
            for (size_t k = i; k < e; k++) if (p[k] != pat(pass, k, p)) errs++;
            if (pass == 0) rd_us += esp_timer_get_time() - t;
            vTaskDelay(1);
        }
    }
    double mb = sz / 1048576.0;
    res_set(R_PSRAM, errs == 0 ? ST_PASS : ST_FAIL,
            "%.2f MB allocated (largest block %u KB), 3 patterns: %u errors; word write %.1f MB/s, read+verify %.1f MB/s",
            mb, (unsigned)(blk >> 10), (unsigned)errs, mb / (wr_us / 1e6), mb / (rd_us / 1e6));
    free(p);
}

// ---------------- step 7: battery and power key ----------------

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_adc_ch;
static bool s_adc_ok, s_cali_ok;

static void battery_init(void)
{
    adc_unit_t unit;
    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_2, .ulp_mode = ADC_ULP_MODE_DISABLE };
    if (adc_oneshot_io_to_channel(PIN_BAT_ADC, &unit, &s_adc_ch) != ESP_OK || unit != ADC_UNIT_2 ||
        adc_oneshot_new_unit(&uc, &s_adc) != ESP_OK) return;
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, s_adc_ch, &cc) != ESP_OK) return;
    adc_cali_curve_fitting_config_t cf = { .unit_id = ADC_UNIT_2, .chan = s_adc_ch, .atten = ADC_ATTEN_DB_12,
                                           .bitwidth = ADC_BITWIDTH_DEFAULT };
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cf, &s_cali) == ESP_OK;
    s_adc_ok = true;
}

// Returns pin millivolts, or -1 when ADC2 is unavailable (Wi-Fi/BLE running) or the read failed.
static int battery_pin_mv(int *raw_out)
{
    if (!s_adc_ok || radio_busy()) return -1;
    int sum = 0, n = 0, raw;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(s_adc, s_adc_ch, &raw) != ESP_OK) return -1;
        sum += raw;
        n++;
    }
    raw = sum / n;
    if (raw_out) *raw_out = raw;
    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) return mv;
    return raw * 3100 / 4095;   // uncalibrated fallback, roughly the 12 dB range
}

static void battery_report(void)
{
    static int last_mv = -1;
    int raw = 0, pin = battery_pin_mv(&raw);
    int key = gpio_get_level(PIN_KEY_BAT);
    if (pin < 0) {
        res_set(R_BATT, s_adc_ok ? ST_INFO : ST_FAIL, "%s; last %d mV; Key_BAT level %d",
                s_adc_ok ? "ADC2 busy (radio on), read skipped" : "ADC2 init failed", last_mv, key);
        return;
    }
    int vbat = pin * BAT_DIVIDER;
    last_mv = vbat;
    bool plausible = vbat >= 2800 && vbat <= 4400;
    res_set(R_BATT, plausible ? ST_PASS : ST_FAIL, "%d mV battery (pin %d mV, raw %d, x%d, %s)%s; Key_BAT level %d",
            vbat, pin, raw, BAT_DIVIDER, s_cali_ok ? "calibrated" : "uncalibrated",
            plausible ? "" : " outside 2.8-4.4 V: no battery fitted?", key);
}

static void key_poll(void)
{
    static bool released_seen, last_pressed, armed_msg, first = true;
    static int64_t hold_start;
    static int presses;
    int64_t now = esp_timer_get_time();
    bool pressed = gpio_get_level(PIN_KEY_BAT) == KEY_PRESSED_LEVEL;
    if (first) {
        first = false;
        res_set(R_KEY, ST_PENDING, "Key_BAT level %d (%s); press it once, hold 3 s to power off on battery",
                gpio_get_level(PIN_KEY_BAT), pressed ? "pressed at boot: release it first" : "released");
    }
    if (!pressed) {
        if (!released_seen) printf("power key: release seen, power-off hold armed\n");
        released_seen = true;
        armed_msg = false;
    }
    if (pressed && !last_pressed) {
        hold_start = now;
        if (released_seen) {
            presses++;
            res_set(R_KEY, ST_PASS, "press #%d seen (Key_BAT level %d = pressed); hold 3 s to power off on battery",
                    presses, gpio_get_level(PIN_KEY_BAT));
        }
    }
    if (!pressed && last_pressed && released_seen && presses)
        printf("power key: released after %lld ms\n", (long long)((now - hold_start) / 1000));
    last_pressed = pressed;
    if (pressed && released_seen && now - hold_start >= 3000000) {
        res_set(R_KEY, ST_INFO, "held 3 s: GPIO%d LOW now (power off if on battery)", PIN_BAT_CTRL);
        disp_set_brightness(0);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(PIN_BAT_CTRL, 0);
        vTaskDelay(pdMS_TO_TICKS(3000));
        // Still running means USB is supplying power, so GPIO16 has no effect. Take the hold back.
        gpio_set_level(PIN_BAT_CTRL, 1);
        disp_set_brightness(0xFF);
        res_set(R_KEY, ST_INFO, "still running 3 s after GPIO%d low: powered from USB, hold restored", PIN_BAT_CTRL);
        released_seen = false;   // require a fresh release before it can fire again
        (void)armed_msg;
    }
}

// ---------------- step 8: SD card (read-only) ----------------

static void step_sd(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SD_MOSI, .miso_io_num = PIN_SD_MISO, .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    esp_err_t e = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) { res_set(R_SD, ST_FAIL, "spi bus: %s", esp_err_to_name(e)); return; }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 20000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI3_HOST;
    sdspi_dev_handle_t h = 0;
    bool host_up = false;
    sdmmc_card_t *card = calloc(1, sizeof *card);
    e = card ? sdspi_host_init() : ESP_ERR_NO_MEM;
    if (e == ESP_OK) { host_up = true; e = sdspi_host_init_device(&slot, &h); }
    if (e == ESP_OK) {
        host.slot = h;
        e = sdmmc_card_init(&host, card);   // identification and CSD read only, no writes
    }
    if (e == ESP_OK) {
        uint64_t bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
        const char *type = card->is_mmc ? "MMC" : (card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC";
        res_set(R_SD, ST_PASS, "%s \"%s\" %.2f GB (%u sectors x %u B), max %d kHz, bus 20 MHz SPI, read-only",
                type, card->cid.name, bytes / 1e9, (unsigned)card->csd.capacity, (unsigned)card->csd.sector_size,
                card->max_freq_khz);
        sdspi_host_remove_device(h);
    } else {
        res_set(R_SD, ST_FAIL, "card init failed: %s (no card inserted?)", esp_err_to_name(e));
        if (h) sdspi_host_remove_device(h);
    }
    if (host_up) sdspi_host_deinit();
    spi_bus_free(SPI3_HOST);
    free(card);
}

// ---------------- touch ----------------

static void touch_poll(void)
{
    static bool down, cleared, had_err_report;
    static int errs, last_x = -100, last_y = -100;
    touch_pt_t pts[2];
    int n = periph_touch_read(pts);
    if (n < 0) {
        if (++errs == 50 && !had_err_report) {
            had_err_report = true;
            res_set(R_TOUCH_PT, ST_FAIL, "FT6336 read errors (50 in a row)");
        }
        return;
    }
    errs = 0;
    if (n == 0) { down = false; cleared = false; last_x = last_y = -100; return; }
    int lx, ly;
    disp_map_touch(pts[0].x, pts[0].y, &lx, &ly);
    if (n == 2 && !cleared) {
        cleared = true;
        printf("two fingers: clearing the screen\n");
        disp_test_pattern();
        return;
    }
    if (n == 1 && cleared) return;   // wait for lift-off after a clear
    bool moved = (lx - last_x) * (lx - last_x) + (ly - last_y) * (ly - last_y) >= 36;
    if (!down || moved) {
        printf("touch n=%d raw(%u,%u) event %u -> landscape(%d,%d)\n", n, pts[0].x, pts[0].y, pts[0].event, lx, ly);
        if (!down) {
            res_set(R_TOUCH_PT, ST_PASS, "touch raw(%u,%u) -> landscape(%d,%d), map: %s", pts[0].x, pts[0].y, lx, ly,
                    disp_touch_map_name());
        }
        down = true;
        last_x = lx;
        last_y = ly;
        disp_dot(lx, ly, 8);
    }
}

// ---------------- main ----------------

void app_main(void)
{
    // Hold battery power first thing: latch the output level before switching the pin to output.
    gpio_set_level(PIN_BAT_CTRL, 1);
    gpio_set_direction(PIN_BAT_CTRL, GPIO_MODE_OUTPUT);
    gpio_config_t key = { .pin_bit_mask = 1ULL << PIN_KEY_BAT, .mode = GPIO_MODE_INPUT };
    gpio_config(&key);

    res_init();
    console_start();
    printf("\n\n##### neuPod ESP32-S3-Touch-AMOLED-2.41 bring-up probe #####\n"
           "GPIO%d driven HIGH (battery hold). Keys: s summary, r rotate mode, t touch map, c clear, b brightness\n\n",
           PIN_BAT_CTRL);

    step_chip();
    step_psram();

    esp_err_t e = periph_i2c_init();
    if (e == ESP_OK) {
        periph_i2c_scan();
        periph_tca_init();   // PWR_EN (EXIO1) must be high before the panel is touched
    } else {
        res_set(R_I2C, ST_FAIL, "i2c bus init: %s", esp_err_to_name(e));
    }

    battery_init();
    battery_report();   // first reading happens before Wi-Fi starts (ADC2)

    bool have_disp = disp_init();
    if (have_disp) {
        disp_test_pattern();
        disp_measure();
        disp_brightness_test();
    }

    if (e == ESP_OK) {
        periph_touch_init();
        periph_imu_probe();
        periph_rtc_probe();
    }

    step_sd();
    res_set(R_WIFI, ST_PENDING, "starting");
    res_set(R_BLE, ST_PENDING, "waiting for Wi-Fi scan");
    radio_start();
    res_print_summary();

    bool bright_full = true;
    int64_t next_batt = esp_timer_get_time() + 2000000;
    for (;;) {
        key_poll();
        if (e == ESP_OK) touch_poll();
        switch (console_take_key()) {
        case 'r': disp_cycle_mode(); break;
        case 't': disp_cycle_touch_map(); break;
        case 'c': disp_test_pattern(); break;
        case 'b':
            bright_full = !bright_full;
            disp_set_brightness(bright_full ? 0xFF : 0x80);
            printf("brightness %s\n", bright_full ? "100%" : "50%");
            break;
        default: break;
        }
        if (esp_timer_get_time() >= next_batt) {
            battery_report();
            next_batt += 2000000;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
