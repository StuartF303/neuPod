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
| PSRAM (QMI CS1) | 47 |

## Display notes

- **Orientation.** The panel's own MADCTL (0x36) is `0xA0` (MY|MV) by default: the controller addresses itself as landscape, with a page
  (row) offset of 16 (the 450 visible columns sit 16 from one end of the controller's 480-column memory and 14 from the other; the
  offset has to be the one on the side the mirror bit makes "first", else a 2 px band of unused memory shows on an edge).
  `-DNEUPOD_AMOLED_LANDSCAPE=3` turns the picture by 180 degrees (MADCTL `0x60`, offset 14) and the touch mapping with it, so the pair stays
  consistent. On the owner's bench, MADCTL `0x60` with the touch mapping below (landscape x = raw y, y = 449 - raw x) was 180 degrees
  apart; the default is therefore the rotated display with that touch mapping. The offsets come from other RM690B0 drivers; whether a
  band shows on an edge has not been checked by eye by me.
- **Even windows.** The controller addresses whole pixel pairs: `hal_display_set_window()` returns `HAL_EINVAL` for an odd x, y, width or
  height. The UI rounds its flush areas outwards (LVGL rounder).
- **Brightness** is the panel's own register (0x51): `hal_display_backlight()` 0..255. `hal_display_sleep()` sends DISPOFF + SLPIN.
- The build selects the UI profile `600x450` automatically.

## Touch notes

`hal_touch_read()` returns the first contact in the display's landscape coordinates (x 0..599, y 0..449). The raw controller values
are portrait (x 0..449, y 0..599): landscape x = raw y, landscape y = 449 - raw x. `-DNEUPOD_AMOLED_TOUCH_LOG=ON` logs every press, a
move every 200 ms and the release with raw and mapped values (off by default: a USB terminal that is attached but not reading can block
the input task). The chip ID line is always logged at init. The input task initialises the controller (it logs its chip, vendor and firmware IDs) and reads the points under the I2C mutex.

## Storage notes

SDHC and SDXC cards only (the library's SPI mode refuses standard-capacity cards). The SPI clock is 20.8 MHz (125 MHz / 6). There is no
SD Status register (so no speed class or allocation unit in the card info), no CMD6 and no bus-clock change in this mode; those HAL calls
answer "unsupported". The write guard, mounting, formatting and the music library work as on the Pico 2 W.

## Power notes

The Power key held for 10 s shows the "shutting down" countdown (`hal_system_can_poweroff()` is true); at the end the runtime lets the
card finish its writes and drops BAT_Control. On battery the board then switches off when the key is released. On USB power it cannot:
the request is refused and the latch is closed again. After dropping the latch the HAL waits for the key to be released (at most 3 s,
in the UI task) and then 0.5 s more; still running means USB power. No VBUS or charger-status line is read (the schematic was not
checked for one). The Power key is read as level; the press that switched the board on is not
counted.

## PSRAM and the big-RAM player (UI-25)

The board's 8 MB PSRAM (APS6404L class, QMI CS1 = GPIO47) holds the big buffers of the **big-RAM player**, which this build has
by default (`NEUPOD_ART_HIRES`, as the simulator and the Y2): 160 x 160 album art with the 24-album cache, art in the album
header, Cover Flow, Playlists, play counts and 128 ratings. `-DNEUPOD_ART_HIRES=OFF` (new build directory or `--clean`) gives
the small player of the Pico 2 W for comparison. Design and budget: [specs/001-amoled-full-art](https://github.com/StuartF303/neuPod/blob/main/specs/001-amoled-full-art/plan.md).

- **Bring-up.** The Pico SDK's `hardware_psram` (board header `PICO_PSRAM_CS_PIN 47`, `PICO_AUTO_DETECT_PSRAM_SIZE 1`: the chip's
  ID is read, nothing is assumed) in `runtime_init`, preceded by our warm-start reset (quad-mode off, reset; a picotool reboot
  or the reset button do not power-cycle the chip). `hal_system_init()` then computes the timing again for 125 MHz (the SDK
  set it for its 150 MHz boot clock, which at 125 MHz would hold CS low 9.2 us, over the chip's 8 us limit), checks the chip
  and zeroes the firmware's PSRAM section (`boards/amoled241/psram_amoled.c`).
- **Placement.** Variables marked `NEUPOD_PSRAM_BSS` (`app/include/psram_bss.h`, empty on every other build) go to the SDK's
  `.psram_noload` section: the art cache (`art_service.c`), the playlist buffers (`playlist_service.c`), the play counts
  (`plays_store.c`) and the play queue (`player.c`).
- **Console.** After the version line: `psram: 8192 KB at 62500 kHz, neupod section N KB`.
- **Check.** After the re-timing (CS low at most 7,500 ns: 14 units of 64 clocks at 125 MHz, about 7.65 us with the tail of an
  access, under the chip's 8 us) one word at every address line and the last word are written and read back through the uncached
  alias before the section is zeroed.
- **Without PSRAM** (no chip, the chip too small, a timing error, a failed read-back) the runtime does not start:
  `boot: psram FAILED: <reason>` every 2 s on the console and a dim red screen (text would need LVGL, whose pool is in PSRAM). The
  Power key held 1 s (after the power-on press is let go) cuts the battery latch, so a board on battery can be switched off.
  `-DNEUPOD_PSRAM_FORCE_FAIL=ON` exercises that path on a healthy board (debug aid).

**Memory (linked, CI 25 Nov 2026, Release; before = `main` at 2f99ded; its RP2350 images are those of 2d95476, the firmware is unchanged between them):**

| | Before | After | Change |
|---|---|---|---|
| Internal RAM, static (`.data` + `.bss`) | 510,376 B | 436,656 B | -73,720 B |
| Internal RAM free above the heap | 11,452 B | 85,172 B | +73,720 B |
| PSRAM (`.psram_noload`) | 0 | 1,507,488 B (of 8 MB) | art cache 1,230,144; LVGL pool 196,608; plays table and snapshot 48,160; play queue 8,992; playlists about 23 KB |
| Flash (`.text` + `.rodata`) | 1,055,916 B | 1,086,616 B | +30,700 B |

The LVGL pool moved to PSRAM at 192 KB (as on the ESP32-S3): the host build with this board's UI options ran out of LVGL memory in
the library tour at the old size. That move is most of the internal RAM freed.

Not checked on the board yet (no board attached when it was built): the boot line, the art and Cover Flow, the frame time
of a Cover Flow step, the LVGL pool peak at 600x450, warm restarts. See the bench checklist in the PR.

## What is not done

- No battery voltage (GPIO40 through the x3 divider) and no RTC / IMU.
- Display and touch orientation, and brightness, have been checked through the registers and the console only.
