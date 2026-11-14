// SPDX-License-Identifier: MPL-2.0
// Wi-Fi scan-only and BLE passive scan. Never connects, never advertises, no credentials, no AP mode.
#pragma once

#include <stdbool.h>

// Spawns the radio task (Wi-Fi scan, then 10 s BLE scan, then radio off). Sets R_WIFI and R_BLE.
void radio_start(void);

// True while a radio is powered. ADC2 (the battery pin) must not be read then; Wi-Fi owns it.
bool radio_busy(void);
