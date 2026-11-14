# Waveshare RP2350-Touch-AMOLED-2.41 (dev board)

The product runtime (`neupod`) also builds for this board, as a second board of the RP2350 platform
(`-DNEUPOD_BOARD=amoled241`; the default, `pico2w`, is unchanged). It is a **dev board**: it has an RP2350B with PSRAM and a
touch AMOLED, which is the shape of the final board's compute and display, but no radio, no audio and no click wheel.

    scripts/build.sh rp2350-amoled241                                 # -> build/rp2350-amoled241/platforms/rp2350/neupod.uf2
    picotool load -f -x build/rp2350-amoled241/platforms/rp2350/neupod.uf2   # a running image reboots into BOOTSEL by itself

(`scripts/build.sh rp2350-amoled241` is `cmake -S . -B build/rp2350-amoled241 -G Ninja -DNEUPOD_PLATFORM=rp2350 -DNEUPOD_BOARD=amoled241`.
`NEUPOD_BOARD` has to be chosen when the build directory is created, because it selects the SDK's board header: use a separate build
directory per board.)

The console is the **USB serial port** (CDC; it shows up as `/dev/serial/by-id/usb-Raspberry_Pi_Pico_*`): the board's UART0 pins are not
brought out. UART0 stays enabled as well (the panic path writes to it).

On a Linux desktop ModemManager probes a new serial port for a minute or so, so a terminal may not be able to open it at once, and the
boot lines printed in the meantime are lost; open the port as soon as it opens and press the reset button (or reflash) to see them again.
The runtime keeps printing a heartbeat and the audio, UI and Wi-Fi statistics every 10 s.

## What is different from the Pico 2 W build

| Part | Pico 2 W | amoled241 |
|------|----------|-----------|
| Board header | SDK `pico2_w` | `platforms/rp2350/boards/amoled241/waveshare_rp2350_touch_amoled_2.41.h` (RP2350B, 16 MB flash) |
| Display | ST7789 320x240, SPI1 + DMA | **RM690B0** AMOLED, 450x600 native, used as **600x450 landscape**; QSPI by one PIO state machine + DMA (`hal_display_rm690b0.c`, `rm690b0_qspi.pio`) |
| Touch | none | **FT6336** on I2C1 (`hal_touch.h`, `hal_touch_ft6336.c`) |
| Input | click wheel (PIO) | no wheel (`hal_input_init` reports "no receiver"; the boot check marks the wheel unavailable); the **Power button** (GPIO24) is the one key |
| Radio | CYW43439 Wi-Fi + Bluetooth, lwIP, BTstack | **none**: `hal_radio_none.c` (Wi-Fi "not present", Bluetooth cannot start; the Wi-Fi and Bluetooth screens show "unavailable") |
| Audio | I2S DAC (Audio Pack) | none: the null sink |
| microSD | SDIO 4-bit | **SPI0** (SCK 18, MOSI 19, MISO 20, CS 23), the same carlk3 library in SPI mode (`hal_storage_spi.c`, `hw_config.c`) |
| Fuel gauge, haptics | MAX17048, DRV2605L on I2C0 | none (the I2C1 bus only answers the addresses of the parts the board has, so those boot checks report "absent" without touching the touch controller) |
| Power | USB | battery with a soft latch: GPIO25 (BAT_Control) is driven high at the start of `hal_system_init`, low = off |
| Status LED | on the CYW43 chip | none |
| Clock | 125 MHz | 125 MHz (QSPI SCLK = 62.5 MHz) |

Not built for this board: the `display_test`, `i2c_test` and `storage_test` executables (Pico 2 W bench tools).

## Pins

| Function | GPIO |
|----------|------|
| AMOLED CS / CLK / D0..D3 / RST / TE / PWR_EN | 9 / 10 / 11..14 / 15 / 16 (unused) / 17 |
| Touch SDA / SCL / INT / RST | 6 / 7 / 4 (unused: polled) / 5 |
| microSD SCK / MOSI / MISO / CS | 18 / 19 / 20 / 23 |
| Power key (KEY_BAT, active low) / BAT_Control | 24 / 25 |
| Battery ADC (x3 divider) | 40 (not read: no battery reading in the UI yet) |
| PSRAM (QMI CS1) | 47 (not used by the firmware yet) |

## Display notes

- **Orientation.** The panel's own MADCTL (0x36) is set to `0x60` (MX|MV): the controller addresses itself as landscape, with a row
  offset of 14 (the 450 visible columns sit 16 from one end of the controller's 480-column memory and 14 from the other). The
  mapping was taken from the controller's other known drivers and checked on the bench only as far as the registers go (the HAL cannot
  see the glass): **if the picture is upside down or mirrored, build with `-DNEUPOD_AMOLED_LANDSCAPE=3`** (MADCTL `0xA0`, offset 16, and
  the touch mapping turned the same way).
- **Even windows.** The controller addresses whole pixel pairs: `hal_display_set_window()` returns `HAL_EINVAL` for an odd x, y, width or
  height. The UI has to round its flush areas outwards before calling (until it does, a strip with an odd edge is not drawn).
- **Brightness** is the panel's own register (0x51): `hal_display_backlight()` 0..255. `hal_display_sleep()` sends DISPOFF + SLPIN.
- Until the UI has a 600x450 profile it draws its 320x240 layout in the top-left corner.

## Touch notes

`hal_touch_read()` returns the first contact in the display's landscape coordinates (x 0..599, y 0..449). The raw controller values
are portrait (x 0..449, y 0..599): landscape x = raw y, landscape y = 449 - raw x. The build logs every press, a move every 200 ms,
and the release with raw and mapped values (`-DNEUPOD_AMOLED_TOUCH_LOG=OFF` to silence it). A small task started by `main()` initialises the
controller once, four seconds after boot, and logs its chip, vendor and firmware IDs (a bring-up aid); the input task is what reads points.

## Storage notes

SDHC and SDXC cards only (the library's SPI mode refuses standard-capacity cards). The SPI clock is 20.8 MHz (125 MHz / 6). There is no
SD Status register (so no speed class or allocation unit in the card info), no CMD6 and no bus-clock change in this mode; those HAL calls
answer "unsupported". The write guard, mounting, formatting and the music library work as on the Pico 2 W.

## Power notes

The Power key held for 10 s shows the "shutting down" countdown (`hal_system_can_poweroff()` is true); at the end the runtime lets the
card finish its writes and drops BAT_Control. On battery the board then switches off when the key is released. On USB power it cannot:
the request is refused and the latch is closed again. The Power key is read as level; the press that switched the board on is not
counted.

## What is not done

- PSRAM (8 MB on QMI CS1) is not initialised by the firmware; the driver of the bring-up probe (`testbed/amoled241/bringup/src/psram.c`)
  is the starting point.
- No battery voltage (GPIO40 through the x3 divider) and no RTC / IMU.
- Display and touch orientation, and brightness, have been checked through the registers and the console only.
