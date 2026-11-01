#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

int main(void)
{
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("neuPod: cyw43_arch_init failed\n");
        return -1;
    }

    bool led = false;
    while (true) {
        led = !led;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
        printf("neuPod alive\n");
        sleep_ms(1000);
    }
}
