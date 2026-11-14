// SPDX-License-Identifier: MPL-2.0
#include "results.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *const NAMES[R_COUNT] = {
    "boot/chip", "psram", "i2c scan", "tca9554", "display", "frame push", "brightness",
    "touch id", "touch point", "imu", "rtc", "battery", "power key", "sd card", "wifi scan", "ble scan",
};
static const char *const ST_TXT[] = { "----", "PASS", "FAIL", "INFO" };

static struct { res_status_t st; char msg[120]; } s_res[R_COUNT];
static SemaphoreHandle_t s_lock;
static volatile char s_key;
static bool s_drv_ok;

void res_init(void) { s_lock = xSemaphoreCreateMutex(); }

void res_set(res_id_t id, res_status_t st, const char *fmt, ...)
{
    char tmp[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_res[id].st = st;
    strlcpy(s_res[id].msg, tmp, sizeof s_res[id].msg);
    xSemaphoreGive(s_lock);
    printf("[%-12s] %s  %s\n", NAMES[id], ST_TXT[st], tmp);
}

void res_print_summary(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    printf("\n===== PROBE SUMMARY  t=%llu s =====\n", (unsigned long long)(esp_timer_get_time() / 1000000));
    int pass = 0, fail = 0, pend = 0;
    for (int i = 0; i < R_COUNT; i++) {
        printf("  %-12s %s  %s\n", NAMES[i], ST_TXT[s_res[i].st], s_res[i].msg);
        if (s_res[i].st == ST_PASS) pass++;
        else if (s_res[i].st == ST_FAIL) fail++;
        else if (s_res[i].st == ST_PENDING) pend++;
    }
    printf("  -> pass %d, fail %d, pending %d.  keys: s summary, r rotate mode, t touch map, c clear, b brightness\n"
           "=====================================\n\n", pass, fail, pend);
    xSemaphoreGive(s_lock);
}

char console_take_key(void)
{
    char k = s_key;
    s_key = 0;
    return k;
}

static void console_task(void *arg)
{
    int64_t next = esp_timer_get_time() + 20000000;
    for (;;) {
        // Short timeout so the 20 s summary stays on schedule; never blocks forever.
        if (s_drv_ok) {
            uint8_t c;
            if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(100)) == 1) {
                if (c == 's') res_print_summary();
                else if (c > ' ') s_key = (char)c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (esp_timer_get_time() >= next) {
            res_print_summary();
            next = esp_timer_get_time() + 20000000;
        }
    }
}

void console_start(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 256;
    cfg.tx_buffer_size = 256;
    s_drv_ok = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    if (!s_drv_ok) printf("console: usb_serial_jtag driver install failed, key input disabled\n");
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
}
