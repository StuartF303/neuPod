// SPDX-License-Identifier: MPL-2.0
/*
 * ESP32-S3 platform bring-up (Waveshare ESP32-S3-Touch-AMOLED-2.41). The console is stdio over the board's native USB-Serial/JTAG
 * port (sdkconfig.defaults), so there is nothing to initialise for it. The board has no status LED.
 */
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_system.h"

hal_status_t hal_system_init(void) {
    // Latch battery power, start the I2C bus and power the panel. A failure here is not fatal to the system: the display and
    // touch HALs report their own state and the boot checks degrade the matching capabilities.
    (void)board_init();
    return HAL_OK;
}

hal_status_t hal_system_late_init(void) { return HAL_OK; } // no radio driver to start (Wi-Fi/BLE come later)

bool hal_panic_can_block(void) { return !xPortInIsrContext(); }

void hal_panic(uint8_t blink_code, const char *msg) {
    (void)blink_code; // no LED on this board
    esp_rom_printf("%s", msg);
    abort(); // the IDF panic handler prints the backtrace and resets
}

size_t hal_stack_free_min(void) { return 0; } // the main task retires; per-task marks are in the monitor's task line

uint64_t hal_micros(void) { return (uint64_t)esp_timer_get_time(); }

uint32_t hal_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void hal_delay_ms(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

void hal_status_led(bool on) { (void)on; } // none on this board

bool hal_chip_id(uint8_t id[8]) {
    // The factory MAC is the chip's unique identity (not secret: docs/wifi.md). Padded to the 8 bytes the interface carries.
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        id[i] = mac[i];
    }
    id[6] = 0x53; // 'S': ESP32-S3 (keeps the key domain apart from other chips)
    id[7] = 0x33;
    return true;
}

// The hardware RNG register. With the radio off (it is, for now) the IDF documents it as pseudo-random from a free-running noise
// source, not a certified TRNG; fine for the per-record nonces of the secrets file, to be revisited when Wi-Fi/BLE are on
// (the RNG is then fed by the RF noise).
void hal_random(void *buf, size_t n) { esp_fill_random(buf, n); }

bool hal_system_can_poweroff(void) { return true; }

bool hal_system_poweroff(void) {
    // Drop the battery latch. On battery the board switches off as soon as the side key is released; on USB power the board
    // stays alive (the charger feeds it), which is a property of the board, not something this call can change.
    gpio_set_level(PIN_BAT_CTRL, 0);
    return true;
}

const char *hal_platform_name(void) { return "esp32s3"; }
