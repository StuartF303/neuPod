// SPDX-License-Identifier: MPL-2.0
// Console input over the native USB-Serial/JTAG port. Output is stdio (the IDF console on the same port). Only the RX path uses
// the driver (the probe proved this arrangement): printf keeps the non-blocking console path, so a missing USB host never stalls
// a task that prints.
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "hal_console.h"

static bool g_tried;
static bool g_ok;

int hal_console_getc_timeout(uint32_t ms) {
    if (!g_tried) { // only the input task reads, so no race on first use
        g_tried = true;
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        cfg.rx_buffer_size = 256;
        cfg.tx_buffer_size = 256;
        g_ok = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    }
    if (!g_ok) {
        return -1;
    }
    uint8_t c;
    int n = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(ms));
    return n == 1 ? (int)c : -1;
}
