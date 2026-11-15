// SPDX-License-Identifier: MPL-2.0
// Board bring-up shared by the HALs: power latch, the one I2C bus, the TCA9554 expander.
#include "board.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_private/startup_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define I2C_TIMEOUT_MS 50

// The battery latch as early as the IDF start-up allows: the first CORE-stage init function, before the heap, the PSRAM and the
// scheduler (the GPIO driver needs none of them). On battery the board stays on only while this pin is high or the key is held,
// so every millisecond before it counts. board_init() repeats it harmlessly.
ESP_SYSTEM_INIT_FN(neupod_power_latch, CORE, BIT(0), 0) {
    gpio_set_level(PIN_BAT_CTRL, 1);
    gpio_set_direction(PIN_BAT_CTRL, GPIO_MODE_OUTPUT);
    return ESP_OK;
}

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_tca;
static bool s_done;
static esp_err_t s_result = ESP_FAIL;

i2c_master_bus_handle_t board_i2c_bus(void) { return s_bus; }

int board_tca_inputs(void) {
    uint8_t reg = 0x00, v = 0;
    if (s_tca == NULL || i2c_master_transmit_receive(s_tca, &reg, 1, &v, 1, I2C_TIMEOUT_MS) != ESP_OK) {
        return -1;
    }
    return v;
}


esp_err_t board_init(void) {
    if (s_done) {
        return s_result;
    }
    s_done = true; // called once from hal_system_init(), before the scheduler runs anything else of ours

    // Hold the battery switch first, before anything can take long: the level is latched before the pin becomes an output.
    gpio_set_level(PIN_BAT_CTRL, 1);
    gpio_set_direction(PIN_BAT_CTRL, GPIO_MODE_OUTPUT);
    gpio_config_t key = {.pin_bit_mask = 1ULL << PIN_KEY_BAT, .mode = GPIO_MODE_INPUT};
    gpio_config(&key);

    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_log_level_set("i2c.master", ESP_LOG_NONE); // an absent chip (battery gauge, haptic driver) NACKs: hal_i2c reports it, the driver need not shout
    esp_err_t e = i2c_new_master_bus(&bc, &s_bus);
    if (e != ESP_OK) {
        printf("board: i2c bus: %s\n", esp_err_to_name(e));
        s_bus = NULL;
        return s_result = e;
    }

    // The TCA9554 expander is deliberately NOT written to. The first versions of this file drove EXIO1 ("PWR_EN") high as an output,
    // as the schematic suggests, and the panel stayed dark; the factory firmware (Espressif/Waveshare example_qspi_with_ram, read out
    // of its image and from Waveshare's demo source) never touches the expander for the display: every EXIO stays at the chip's
    // power-on default (input) and the panel works. Only its input register is read here, so the boot log shows the level.
    i2c_master_dev_handle_t tca = NULL;
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_TCA9554, .scl_speed_hz = 400000};
    e = i2c_master_bus_add_device(s_bus, &dc, &tca);
    if (e == ESP_OK) {
        s_tca = tca;
        int in = board_tca_inputs();
        printf("board: TCA9554 left at its power-on defaults, inputs 0x%02X (EXIO0 TE, EXIO1 PWR_EN, EXIO2 TP_INT)\n", in < 0 ? 0xFF : in);
        e = in < 0 ? ESP_FAIL : ESP_OK;
    }
    if (e != ESP_OK) {
        printf("board: TCA9554 (I2C 0x20) did not answer: %s\n", esp_err_to_name(e));
    }
    return s_result = e;
}