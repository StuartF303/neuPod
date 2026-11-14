// SPDX-License-Identifier: MPL-2.0
/**
 * @file board.h
 * @brief Waveshare ESP32-S3-Touch-AMOLED-2.41 (ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM): pin map and shared bring-up.
 *
 * Pins are from the board schematic and were proven by the bring-up probe (testbed/esp32amoled241/bringup/main/board.h).
 * Private to platforms/esp32s3/main/hal.
 */
#ifndef NEUPOD_ESP32S3_BOARD_H
#define NEUPOD_ESP32S3_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/* AMOLED RM690B0 on QSPI (SPI2). RST is a plain GPIO; the panel's power enable and TE are on the TCA9554 expander. */
#define PIN_LCD_CS 9
#define PIN_LCD_CLK 10
#define PIN_LCD_D0 11
#define PIN_LCD_D1 12
#define PIN_LCD_D2 13
#define PIN_LCD_D3 14
#define PIN_LCD_RST 21

/* One I2C bus: TCA9554 expander, FT6336 touch, QMI8658 IMU, PCF85063 RTC. */
#define PIN_I2C_SDA 47
#define PIN_I2C_SCL 48
#define PIN_TOUCH_RST 3

/* microSD on SPI (SPI3). */
#define PIN_SD_CS 2
#define PIN_SD_SCK 4
#define PIN_SD_MOSI 5
#define PIN_SD_MISO 6

/* Power: KEY_BAT senses the side key (active low), BAT_CTRL is the soft latch (drive HIGH early or the board switches itself
 * off on battery when the key is released; LOW = off), BAT_ADC is the battery divider (x3, ADC2: not read, see hal_system.c). */
#define PIN_KEY_BAT 15
#define PIN_BAT_CTRL 16
#define PIN_BAT_ADC 17
#define KEY_PRESSED_LEVEL 0

/* TCA9554 expander (EXIO bit numbers). */
#define EXIO_TE 0
#define EXIO_PWR_EN 1
#define EXIO_TP_INT 2
#define I2C_ADDR_TCA9554 0x20
#define I2C_ADDR_FT6336 0x38

/* Panel: native 450 x 600 portrait RAM, used as 600 x 450 landscape with MADCTL 0x30; the visible area starts 16 rows into RAM. */
#define PANEL_NATIVE_W 450
#define PANEL_NATIVE_H 600
#define PANEL_RAM_OFFSET 16
#define LAND_W 600
#define LAND_H 450

/**
 * One-time board bring-up, from hal_system_init(): latch the battery switch, start the I2C bus and raise the panel power enable
 * (EXIO1) on the expander. Idempotent. Returns ESP_OK if the bus and the expander answered; the board still runs without them
 * (the panel then stays dark and the display HAL reports it).
 */
esp_err_t board_init(void);

/** The shared I2C bus (the i2c_master driver locks the bus per transaction itself). NULL if board_init() could not create it. */
i2c_master_bus_handle_t board_i2c_bus(void);

#endif // NEUPOD_ESP32S3_BOARD_H
