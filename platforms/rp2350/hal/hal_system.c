// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>

#include "hal_system.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

hal_status_t hal_system_init(void) {
    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("neuPod: cyw43_arch_init failed\n");
        return HAL_ENODEV;
    }
    return HAL_OK;
}

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

void hal_delay_ms(uint32_t ms) { sleep_ms(ms); }

void hal_status_led(bool on) { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on); }

const char *hal_platform_name(void) { return "rp2350"; }
