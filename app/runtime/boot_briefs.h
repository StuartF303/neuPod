// SPDX-License-Identifier: MPL-2.0
// Short captions for the boot splash, one per kind of failure. They must fit on ONE caption line next
// to a "[n/m] name: " prefix (tests/test_boot_briefs.c measures them with the real font); the full
// reason goes to the UART. Keep each at 24 characters or fewer.
#ifndef NEUPOD_BOOT_BRIEFS_H
#define NEUPOD_BOOT_BRIEFS_H

#define BR_DISPLAY_INIT "init failed"
#define BR_SD_NONE "no card"
#define BR_SD_INIT "card init failed"
#define BR_SD_INFO "card unreadable"
#define BR_SD_NOFS "no filesystem"
#define BR_SD_MOUNT "mount failed"
#define BR_I2C_BUSY "i2c bus busy"
#define BR_I2C_INIT "i2c init failed"
#define BR_GAUGE "gauge: no response"
#define BR_NO_CELL "no battery cell"
#define BR_BAD_CELL "bad cell voltage"
// Haptics boot check wording: the DRV2605L at 0x5A on the product, the timed_output ERM vibrator on the Y2.
#ifdef NEUPOD_PLATFORM_Y2
#define BR_HAPTIC "no vibrator node"
#define HAPTIC_FAIL_FMT "timed_output vibrator: %s"
#define HAPTIC_OK_MSG "timed_output vibrator (ERM)"
#else
#define BR_HAPTIC "no response at 0x5A"
#define HAPTIC_FAIL_FMT "driver (0x5A): %s"
#define HAPTIC_OK_MSG "DRV2605L, standby"
#endif
#define BR_WHEEL_RX "receiver error"
#define BR_WHEEL_OFF "not connected"
#define BR_WHEEL_NOFRAME "no frames"
#define BR_AUDIO "no audio output"
#define BR_RADIO_NONE "not implemented"
#define BR_RADIO_CHIP "radio chip failed"
#define BR_BT_START "bluetooth not started"
#define BR_BT_HCI "bluetooth stack down"
#define BR_TIMEOUT "check timed out"
#define BR_NOMEM "out of memory"

#define BOOT_BRIEF_ALL                                                                             \
    BR_DISPLAY_INIT, BR_SD_NONE, BR_SD_INIT, BR_SD_INFO, BR_SD_NOFS, BR_SD_MOUNT, BR_I2C_BUSY,      \
        BR_I2C_INIT, BR_GAUGE, BR_NO_CELL, BR_BAD_CELL, BR_HAPTIC, BR_WHEEL_RX, BR_WHEEL_OFF,       \
        BR_WHEEL_NOFRAME, BR_AUDIO, BR_RADIO_NONE, BR_RADIO_CHIP, BR_BT_START, BR_BT_HCI, BR_TIMEOUT, BR_NOMEM

#endif
