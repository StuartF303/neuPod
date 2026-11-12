// SPDX-License-Identifier: MPL-2.0
#include "ui_licence_data.h"

#include <string.h>

// Keep in step with the table in LICENSE.md. Notices are short plain sentences (no code, no paths): they are read on a 2.5 inch screen.
static const ui_licence_t k_licences[] = {
    {"neuPod firmware", "MPL-2.0",
     "The firmware is under the Mozilla Public License 2.0, the hardware designs under CERN-OHL-S-2.0 and the documentation under CC BY 4.0. "
     "Source: github.com/StuartF303/neuPod",
     "Firmware and software", "Mozilla", false},
    {"FreeRTOS kernel", "MIT",
     "Copyright Amazon.com, Inc. or its affiliates. The Raspberry Pi port files carry a BSD-3-Clause notice. Runs the tasks of the player.",
     "FreeRTOS", "MIT", false},
    {"LVGL", "MIT", "Copyright (c) LVGL Kft. The graphics library that draws every screen.", "LVGL", "MIT", false},
    {"Montserrat font", "SIL OFL 1.1",
     "Copyright The Montserrat Project Authors. Built into the firmware as letter images in the sizes the screens use (12 to 48). The licence text is in the source tree, next to the fonts.",
     "Montserrat", "OFL", false},
    {"Lucide icons", "ISC / MIT",
     "Copyright Lucide Icons and Contributors (ISC). The arrows, lock, folder and other small icons in the lists and the status bar; a few of them come from Feather, "
     "copyright Cole Bemis (MIT). lucide.dev",
     "Lucide", "ISC", false},
    {"Font Awesome icons", "CC BY 4.0 / OFL",
     "Font Awesome Free by Fonticons, Inc. The icons are CC BY 4.0 and the font files SIL OFL 1.1. They are the few symbols still drawn from the fonts: the music note, "
     "the start-up icons and the tick. fontawesome.com/license/free",
     "Font Awesome", "CC BY 4.0", false},
    {"FatFs", "BSD-style",
     "FatFs R0.15, copyright ChaN. Free to use with the copyright notice and disclaimer kept. Reads and writes the memory card. elm-chan.org/fsw/ff",
     "FatFs R0.15", "BSD-style", false},
    {"TJpgDec", "BSD-style",
     "Tiny JPEG Decompressor R0.03, copyright 2021 ChaN. Free for personal and commercial use with the notice kept. Draws the album art.", "TJpgDec",
     "BSD-style", false},
    {"dr_flac, dr_mp3", "Public domain / MIT-0",
     "David Reid (dr_libs). dr_mp3 contains minimp3 by lieff (CC0). These decode FLAC and MP3 files.", "dr_flac", "MIT No Attribution", false},
    {"Apple Lossless", "Apache-2.0",
     "Copyright (c) 2011 Apple Inc. The ALAC decoder, changed to C with its own memory handling. The licence text is in the source tree. Plays ALAC files.",
     "Apple Lossless", "Apache", false},
    {"mbed TLS", "Apache-2.0",
     "Copyright The Mbed TLS Contributors, used under the Apache 2.0 option. AES-GCM, SHA-256 and HKDF keep the saved Wi-Fi passwords safe on the card.",
     "mbed TLS 3.6", "Apache-2.0", false},
    {"BTstack", "Non-commercial",
     "BlueKitchen GmbH. Free for non-commercial use only: any commercial product needs a licence from BlueKitchen (bluekitchen-gmbh.com). "
     "On Raspberry Pi hardware a separate grant applies (not on the Y2 testbed). LICENSE.md in the source has the details.",
     "BTstack (BlueKitchen GmbH)", "non-commercial", false},
    {"Android SBC codec", "Apache-2.0",
     "Bluedroid SBC from the Android project, copyright Broadcom 1999-2012, inside BTstack. It encodes the audio sent to a Bluetooth headset.",
     "Bluedroid SBC", "Apache", false},
    {"lwIP", "BSD-3-Clause",
     "Copyright Swedish Institute of Computer Science (Adam Dunkels). The network stack behind Wi-Fi.", "lwIP", "BSD-3-Clause", true},
    {"cyw43-driver", "Non-commercial",
     "George Robotics. Free for personal non-commercial use; with a Raspberry Pi chip the Raspberry Pi licence applies. The radio firmware is Infineon's.",
     "cyw43-driver", "non-commercial", true},
    {"SD card driver", "Apache-2.0",
     "no-OS-FatFS-SD-SDIO-SPI-RPi-Pico by Carl John Kugler III. Talks to the memory card on the RP2350 board.", "no-OS-FatFS-SD-SDIO-SPI-RPi-Pico",
     "Apache-2.0", true},
    {"Pico SDK", "BSD-3-Clause", "Copyright Raspberry Pi (Trading) Ltd. The support library of the RP2350 chip.", "Pico SDK", "BSD-3-Clause", true},
};

#define N_LICENCES (sizeof k_licences / sizeof k_licences[0])

static bool shown(const ui_licence_t *l, const char *platform) {
    return !l->rp2350_only || (platform != NULL && (strcmp(platform, "rp2350") == 0 || strcmp(platform, "host") == 0));
}

const ui_licence_t *ui_licence_all(size_t *count) {
    if (count != NULL) {
        *count = N_LICENCES;
    }
    return k_licences;
}

size_t ui_licence_count(const char *platform) {
    size_t n = 0;
    for (size_t i = 0; i < N_LICENCES; i++) {
        n += shown(&k_licences[i], platform) ? 1u : 0u;
    }
    return n;
}

const ui_licence_t *ui_licence_get(const char *platform, size_t i) {
    for (size_t k = 0; k < N_LICENCES; k++) {
        if (shown(&k_licences[k], platform) && i-- == 0) {
            return &k_licences[k];
        }
    }
    return NULL;
}
