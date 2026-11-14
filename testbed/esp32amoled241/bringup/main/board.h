// SPDX-License-Identifier: MPL-2.0
// Pin map of the Waveshare ESP32-S3-Touch-AMOLED-2.41 (from its schematic and factory firmware).
#pragma once

#define PIN_LCD_CS      9
#define PIN_LCD_CLK     10
#define PIN_LCD_D0      11
#define PIN_LCD_D1      12
#define PIN_LCD_D2      13
#define PIN_LCD_D3      14
#define PIN_LCD_RST     21

#define PIN_I2C_SDA     47
#define PIN_I2C_SCL     48
#define PIN_TOUCH_RST   3

#define PIN_SD_CS       2
#define PIN_SD_SCK      4
#define PIN_SD_MOSI     5
#define PIN_SD_MISO     6

#define PIN_KEY_BAT     15   // power key sense
#define PIN_BAT_CTRL    16   // drive HIGH to hold battery power
#define PIN_BAT_ADC     17   // ADC2 channel 6, divider 200k/100k
#define BAT_DIVIDER     3

// TCA9554 expander bits
#define EXIO_TE         0
#define EXIO_PWR_EN     1
#define EXIO_TP_INT     2
#define EXIO_IMU_INT2   3
#define EXIO_RTC_INT    4

#define I2C_ADDR_TCA9554 0x20
#define I2C_ADDR_FT6336  0x38
#define I2C_ADDR_QMI8658 0x6B  /* scan on the bench 14 Nov 2026: 0x6B (SA0 high), as on the RP2350 sibling */
#define I2C_ADDR_PCF85063 0x51

// Panel native geometry is 450 x 600 (portrait); the probe presents 600 x 450 landscape.
#define PANEL_W         450
#define PANEL_H         600
#define PANEL_OFFSET    16   // the RM690B0 visible area starts 16 columns/rows into its RAM
#define LAND_W          600
#define LAND_H          450

// Power key is assumed active-low (pull-up to 3V3, key to ground); unverified, see README.
#define KEY_PRESSED_LEVEL 0
