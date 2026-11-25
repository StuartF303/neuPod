# Waveshare demo code (third party)

Copied from the Waveshare RP2350-Touch-AMOLED-2.41 demo package (`RP2350-Touch-AMOLED-2.41.zip` from the Waveshare wiki,
`C/01-LCD/lib/` and `C/boards/`), downloaded 2026-11-14. Files keep their original headers; do not strip them.

| Directory | Files | Licence as stated in the file headers |
|-----------|-------|----------------------------------------|
| `Config/` | `DEV_Config.c/.h`, `Debug.h` | MIT permission notice (Waveshare Team), kept in each header |
| `QSPI_PIO/` | `qspi.pio`, `qspi_pio.c/.h` | same MIT notice (`qspi.pio` has no header; same package) |
| `AMOLED/` | `AMOLED_2in41.c/.h` | same MIT notice |
| `Touch/` | `FT6336U.h` (register map only) | same MIT notice |
| `Fonts/` | `fonts.h`, `font16.c`, `font24.c` | BSD-3-Clause, (c) 2014 STMicroelectronics (full text in each file) |
| `../../boards/` | `waveshare_rp2350_touch_amoled_2.41.h` | BSD-3-Clause, (c) 2020 Raspberry Pi (Trading) Ltd (SPDX line in the file) |

The Waveshare package carries no separate LICENSE file; the MIT text is the per-file permission notice, which says the
copyright notice and the permission notice "shall be included in all copies or substantial portions of the Software".
Keeping the files whole satisfies that.

## neuPod changes (marked "neuPod change" in the files)

- `QSPI_PIO/qspi_pio.h`: `PIN_PWR_EN` -1 -> 17 (the schematic's PWR_EN; -1 was cast to a uint8_t 255 and passed to `gpio_init`).

Not copied: the rest of the demo (GUI_Paint, other fonts, Chinese fonts, QMI8658, PCF85063A, FT6336U.c, the PSRAM helper and TLSF allocator,
the FatFs example and its SD driver). The probe has its own small I2C and SD code.

The PSRAM helper (`psram_tool`) was removed again: it carried no licence notice. `src/psram.c` is our own MPL-2.0 replacement, written from the RP2350 and APS6404L datasheets.
