// SPDX-License-Identifier: MPL-2.0
// Board bring-up shared by the HALs: power latch, the one I2C bus, the TCA9554 expander.
#include "board.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "esp_private/startup_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_psram.h"
#include "sdkconfig.h"

// extram_bss bounds (main/linker.lf): the static buffers the firmware keeps in PSRAM.
extern int _ext_ram_bss_start, _ext_ram_bss_end;

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
    uint8_t reg = TCA9554_REG_INPUT, v = 0;
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

    // The same PSRAM summary line as the RP2350 AMOLED board (UI-25, FR-008). The IDF stops the boot itself if the PSRAM is missing.
    printf("psram: %u KB at %u kHz, neupod section %u KB\n", (unsigned)(esp_psram_get_size() / 1024u), (unsigned)CONFIG_SPIRAM_SPEED * 1000u,
           (unsigned)(((uintptr_t)&_ext_ram_bss_end - (uintptr_t)&_ext_ram_bss_start) / 1024u));

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

    // The TCA9554 expander must be configured before the panel is initialised (bench finding, 21 Nov 2026). At a cold power-on it
    // reads input 0xFE, output 0xFF, polarity 0x00, config 0xFF (every EXIO an input) and the panel stays dark. After the factory
    // firmware has run it reads output 0x03 and config 0xDC: EXIO0 and EXIO1 (PWR_EN) driven high, EXIO5 driven low, the rest inputs,
    // and the panel lights. That state survives an ESP32 reset and is lost only on a power cycle, which is why earlier "fixes" (and
    // the older belief that the factory never writes the expander) appeared to work from a warm expander. The output register is
    // written first so the pins come up at the right level when the config register turns them into outputs. Verified lighting the
    // panel from a full cold power cycle. hal_display_init() runs after this (hal_system_init() precedes it in main()).
    i2c_master_dev_handle_t tca = NULL;
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = I2C_ADDR_TCA9554, .scl_speed_hz = 400000};
    e = i2c_master_bus_add_device(s_bus, &dc, &tca);
    if (e == ESP_OK) {
        s_tca = tca;
        const uint8_t out_seq[2] = {TCA9554_REG_OUTPUT, TCA9554_OUTPUT_INIT};
        const uint8_t cfg_seq[2] = {TCA9554_REG_CONFIG, TCA9554_CONFIG_INIT};
        e = i2c_master_transmit(s_tca, out_seq, sizeof out_seq, I2C_TIMEOUT_MS);
        if (e == ESP_OK) {
            e = i2c_master_transmit(s_tca, cfg_seq, sizeof cfg_seq, I2C_TIMEOUT_MS);
        }
        if (e == ESP_OK) {
            esp_rom_delay_us(TCA9554_SETTLE_US); // busy-wait: safe whether or not the scheduler is running yet
            uint8_t reg_out = TCA9554_REG_OUTPUT, reg_cfg = TCA9554_REG_CONFIG, out = 0, cfg = 0;
            bool out_ok = i2c_master_transmit_receive(s_tca, &reg_out, 1, &out, 1, I2C_TIMEOUT_MS) == ESP_OK;
            bool cfg_ok = i2c_master_transmit_receive(s_tca, &reg_cfg, 1, &cfg, 1, I2C_TIMEOUT_MS) == ESP_OK;
            int in = board_tca_inputs();
            char sin[3] = "??", sout[3] = "??", scfg[3] = "??"; // a failed read prints ??, not a plausible-looking value
            if (in >= 0) {
                snprintf(sin, sizeof sin, "%02X", (unsigned)in);
            }
            if (out_ok) {
                snprintf(sout, sizeof sout, "%02X", (unsigned)out);
            }
            if (cfg_ok) {
                snprintf(scfg, sizeof scfg, "%02X", (unsigned)cfg);
            }
            printf("board: TCA9554 inputs 0x%s output 0x%s config 0x%s (wrote 0x%02X / 0x%02X)\n", sin, sout, scfg, TCA9554_OUTPUT_INIT,
                   TCA9554_CONFIG_INIT);
            e = (in >= 0 && out_ok && cfg_ok) ? ESP_OK : ESP_FAIL;
        }
    }
    if (e != ESP_OK) {
        printf("board: TCA9554 (I2C 0x20) init failed: %s\n", esp_err_to_name(e));
    }
    return s_result = e;
}