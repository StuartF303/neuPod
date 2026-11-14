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
| `PSRAM/` | `psram_tool.c/.h` | **No licence notice in the files.** Header says it is for 4D Systems RP2350 boards; the Waveshare package ships it inside its MIT-notice tree. Provenance unclear; dev-only testbed use, not for distribution |
| `../../boards/` | `waveshare_rp2350_touch_amoled_2.41.h` | BSD-3-Clause, (c) 2020 Raspberry Pi (Trading) Ltd (SPDX line in the file) |

The Waveshare package carries no separate LICENSE file; the MIT text is the per-file permission notice, which says the
copyright notice and the permission notice "shall be included in all copies or substantial portions of the Software".
Keeping the files whole satisfies that.

## neuPod changes (marked "neuPod change" in the files)

- `QSPI_PIO/qspi_pio.h`: `PIN_PWR_EN` -1 -> 17 (the schematic's PWR_EN; -1 was cast to a uint8_t 255 and passed to `gpio_init`).
- `PSRAM/psram_tool.c/.h`: the raw PSRAM ID bytes are kept in `rp_psram_kgd` / `rp_psram_eid` so the probe can print them.

Not copied: the rest of the demo (GUI_Paint, other fonts, Chinese fonts, QMI8658, PCF85063A, FT6336U.c, the TLSF allocator,
the FatFs example and its SD driver). The probe has its own small I2C and SD code.
