// SPDX-License-Identifier: MPL-2.0
/*
 * Waveshare RP2350-Touch-AMOLED-2.41 (NEUPOD_BOARD=amoled241): pin plan and panel constants shared by the board's HAL files.
 * Facts proven on the bench with testbed/amoled241/bringup (schematic RP2350-Touch-AMOLED-2.41.pdf).
 */
#ifndef NEUPOD_AMOLED241_BOARD_H
#define NEUPOD_AMOLED241_BOARD_H

// AMOLED RM690B0, QSPI driven by a PIO state machine (native 450x600 portrait, used as 600x450 landscape).
#define AMOLED_PIN_CS 9
#define AMOLED_PIN_CLK 10
#define AMOLED_PIN_D0 11 // D0..D3 = GPIO 11..14
#define AMOLED_PIN_RST 15
#define AMOLED_PIN_TE 16 // tearing-effect output of the panel; not used
#define AMOLED_PIN_PWR_EN 17 // panel supply enable (schematic PWR_EN)

// FT6336 touch controller on I2C1 (shared with the RTC PCF85063 and the IMU QMI8658).
#define TOUCH_I2C_ADDR 0x38
#define TOUCH_PIN_INT 4
#define TOUCH_PIN_RST 5

// Power: KEY_BAT senses the power button (active low, pull-up), BAT_Control is the soft latch of the battery switch.
#define BOARD_PIN_KEY_BAT 24
#define BOARD_PIN_BAT_CONTROL 25 // drive HIGH to stay powered on battery, LOW to switch off
#define BOARD_PIN_BAT_ADC 40     // battery through a x3 divider (200k / 100k); not read by the firmware yet

// Landscape rotation: 1 = MADCTL 0x60 (MX|MV), 3 = MADCTL 0xA0 (MY|MV), the panel turned the other way. Both the display and the
// touch mapping follow it. -DNEUPOD_AMOLED_LANDSCAPE=3 on the cmake command line flips the picture (and the touch) by 180 degrees.
#ifndef NEUPOD_AMOLED_LANDSCAPE
#define NEUPOD_AMOLED_LANDSCAPE 1
#endif

#define AMOLED_NATIVE_W 450u // portrait, as the controller's memory sees it
#define AMOLED_NATIVE_H 600u
#define AMOLED_WIDTH 600u // landscape, what the HAL reports
#define AMOLED_HEIGHT 450u

#ifdef __cplusplus
extern "C" {
#endif
/** Pull-up on the Power key's pin (hal_input_amoled.c); safe to call repeatedly, and early (hal_system_init does). */
void hal_input_power_key_setup(void);
/** Hook run before the battery switch is released (hal_system.c): the runtime flushes the card here. */
void amoled241_set_poweroff_prepare(void (*fn)(void));
#ifdef __cplusplus
}
#endif

#endif // NEUPOD_AMOLED241_BOARD_H
