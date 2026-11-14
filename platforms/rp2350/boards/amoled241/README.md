# Waveshare RP2350-Touch-AMOLED-2.41 (`NEUPOD_BOARD=amoled241`)

Board-specific HAL files for the RP2350 platform's second board. Everything about the board, its build and its differences from the
Pico 2 W is in [docs/rp2350-amoled241.md](../../../../docs/rp2350-amoled241.md).

    scripts/build.sh rp2350-amoled241        # build/rp2350-amoled241/platforms/rp2350/neupod.uf2

| File | What |
|------|------|
| `waveshare_rp2350_touch_amoled_2.41.h` | SDK board header (BSD-3-Clause, Raspberry Pi; copied from the bring-up probe, see `LICENSE.md`) |
| `board.h` | pins and panel constants |
| `hal_display_rm690b0.c`, `rm690b0_qspi.pio` | RM690B0 AMOLED over PIO QSPI + DMA (`hal_display.h`) |
| `hal_touch_ft6336.c` | FT6336 touch (`hal_touch.h`) |
| `hal_input_amoled.c` | no wheel; the Power key (`hal_input.h`) |
| `hal_radio_none.c` | no Wi-Fi, no Bluetooth (`hal_radio.h`) |
| `hal_storage_spi.c`, `hw_config.c` | microSD on SPI0 through the carlk3 library (`hal_storage.h`) |

Shared files with board switches: `../../hal/hal_system.c` (power latch, no CYW43 LED) and `../../hal/hal_i2c.c` (I2C1 on GP6/GP7, only the
board's own addresses answer).
