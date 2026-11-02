// SPDX-License-Identifier: MPL-2.0
#include "hal_system.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

hal_status_t hal_system_init(void) { return HAL_OK; }

size_t hal_stack_free_min(void) { return 0; }

uint64_t hal_micros(void) { return (uint64_t)esp_timer_get_time(); }

uint32_t hal_millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void hal_delay_ms(uint32_t ms) {
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}

void hal_status_led(bool on) {
    // TODO: status LED pin/type depends on the ESP32-S3 comparison board (not yet chosen).
    (void)on;
}

const char *hal_platform_name(void) { return "esp32s3"; }
