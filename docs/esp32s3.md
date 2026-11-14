# ESP32-S3 platform (Waveshare ESP32-S3-Touch-AMOLED-2.41)

The full neuPod runtime on an ESP32-S3 dev board: boot self-checks, settings, storage and library, the LVGL UI, input, and the
audio core with a **null sink** (the board has no audio hardware). It is built from the same `app/` sources as the RP2350
`neupod` target, with a board HAL in `platforms/esp32s3/main/hal/`. This is a **dev platform**, not a product target (the
product target is the RP2350B, [ADR-0001](adr/0001-rp2350-phase1-platform.md)); it exists to run the real runtime on a touch AMOLED
panel and to prove the HAL interfaces on a third architecture.

**Not in yet:** Wi-Fi and BLE (the radio HAL answers "not present" and nothing joins a network), a battery gauge (the board has no
fuel gauge; the battery divider on GPIO17 is not read), the IMU and RTC.

| | |
|---|---|
| Chip | ESP32-S3R8: dual Xtensa LX7 at 240 MHz, 8 MB octal PSRAM, 16 MB flash. Wi-Fi + BLE 5 only: **no Classic Bluetooth, so no A2DP** |
| Display | 450 x 600 RM690B0 AMOLED on QSPI (SPI2: CS 9, CLK 10, D0-3 11-14, RST 21), used as **600 x 450 landscape** (MADCTL 0x30). Panel power enable and TE are on the TCA9554 expander (EXIO1, EXIO0) |
| Touch | FT6336 at I2C 0x38, reset GPIO3, polled (its interrupt is EXIO2) |
| I2C | one bus, SDA 47 / SCL 48: TCA9554 0x20, FT6336 0x38, QMI8658 0x6B, PCF85063 0x51 |
| microSD | SPI (SPI3): CS 2, SCK 4, MOSI 5, MISO 6, 20 MHz |
| Power | `KEY_BAT` GPIO15 (side key, active low), `BAT_CTRL` GPIO16 (soft latch: **driven high first thing**, or the board switches off on battery when the key is released), `BAT_ADC` GPIO17 |
| Console | the USB-C port, native USB-Serial/JTAG (`303a:1001`), stdio plus the key map of [Runtime](runtime.md#input-sources-and-the-testbed-key-map) |

The pin map and the panel facts were proven by the bring-up probe ([`testbed/esp32amoled241/bringup`](https://github.com/StuartF303/neuPod/tree/main/testbed/esp32amoled241/bringup)).

## Build and flash

Same container as CI (ESP-IDF v5.3.2). From the repository root, with the submodules fetched (`git submodule update --init lib/lvgl`):

```sh
docker run --rm --user $(id -u):$(id -g) -e HOME=/tmp -v "$PWD":/project -w /project espressif/idf:v5.3.2 \
  bash -c '. $IDF_PATH/export.sh && scripts/build.sh esp32s3'
```

or, with ESP-IDF installed, `. ~/esp/esp-idf/export.sh && scripts/build.sh esp32s3`. The result is `build/esp32s3/neupod.bin` (about 1 MB
of a 4 MB app partition). Flash the bootloader, partition table and app (**this replaces whatever is on the board**; the factory image
is restored with `esptool write-flash 0 factory-flash-16MB.bin` from a backup taken first):

```sh
cd build/esp32s3
esptool --chip esp32s3 --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_* -b 460800 write-flash @flash_args
```

The console is the same USB port (115200 is nominal; it is USB). Keys typed there drive the UI like the Pico rig's UART.

## How it differs from the other platforms

**FatFs.** ESP-IDF ships its own `fatfs` component (`ff.h`, `diskio.h`, `f_*` and `disk_*` symbols), which collides with
neuPod's. The build **excludes the IDF component entirely** (`EXCLUDE_COMPONENTS fatfs` in `platforms/esp32s3/CMakeLists.txt`) and
uses `app/third_party/fatfs` with the shared `ffconf.h` and `app/storage/diskio.c` over `hal_storage`, the same layering as the Y2 and
the host. `hal_storage` talks to the card through IDF's `sdspi` host and `sdmmc` protocol code (identification, sector read and write).
The reasons: the card's file system behaviour (exFAT, long names, the `FF_*` options the library and settings code were tested with)
stays identical on every board, the vendored copy has one configuration check, and there is no `ff.h` on any include path to pick the wrong one.
IDF's VFS/`esp_vfs_fat` layer is not used: `nfs.h` is the file API.

**RTOS.** `app/os/os_freertos.c` builds unchanged apart from three guarded items: the FreeRTOS headers live under `freertos/`; every runtime task
is created with `xTaskCreatePinnedToCore(..., core 0)`; and `os_start()` retires `app_main`'s task (the IDF scheduler is already running). Stacks are
**doubled** (`OS_STACK_SCALE 2`): Xtensa's windowed-register frames and newlib's `printf` are larger than Thumb-2's, and the library scan overflowed
the 8 KB storage stack on the bench (the hardware stack-end watchpoint, `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`, is on to catch the next one).

**The audio core (ADR-0011).** The RP2350's core 1 is bare metal; the S3's FreeRTOS is SMP, so the shape is kept with tasks:

| Where | What |
|---|---|
| CPU 0 | every RTOS task of the runtime (storage, UI, input, monitor, check tasks, haptic, wifi) and all IDF service tasks |
| CPU 1 | `audio`: the audio core loop (`audio_core_main`, priority 5, 28 KB stack), and `vclk`: the block clock task (priority 20) |

`hal_audio.c` is the null sink with a real-time block clock: a 1 MHz general-purpose timer interrupt on CPU 1 (allocated by the core that calls
`hal_audio_open()`, deadlines absolute and drift-free) wakes `vclk`, which calls the block callback. The callback was written for interrupt context,
so a task that preempts the decoder on the same CPU is a safe home for it. Measured: 5804 us period, jitter under 70 us, 0 under-runs. The core-1
rules scripts (`check_core1_rules`, the closure check) apply to the RP2350 image only.

**Memory.** 512 KB internal SRAM is shared by the static data, task stacks and DMA buffers; the 8 MB PSRAM takes the big buffers. By object file,
in `main/linker.lf` (no attributes in `app/`):

| In PSRAM | Why there |
|---|---|
| codec arena (99 KB), compressed-data ring (32 KB) | decoder working memory and the SD-to-decoder ring; not latency critical |
| UI render buffers (72 KB at 600x450) | too big for internal RAM next to the stacks |
| LVGL's pool (192 KB, `NEUPOD_LVGL_POOL_BYTES`) | widgets, styles, layers; sized like the hosted builds: 72 KB ran out on the Music screens |
| display shadow (527 KB, allocated at run time) | see below |

Internal: the PCM ring (32 KB), the library buffers, a 2 KB SD staging buffer (PSRAM or unaligned buffers are copied through it, so no read or write depends on a bounce-buffer allocation), the display's two DMA strips (19 KB each), task stacks. After boot about 76 KB of
internal heap is free (32 KB at the lowest, while the boot-check tasks run together): enough now, but the Wi-Fi/BLE stacks will need to
be budgeted against it (trim the doubled stacks that measure small: the monitor line prints each task's stack-free minimum).

**Display.** `hal_display.c` talks to the RM690B0 through `esp_lcd`'s SPI panel IO in QSPI mode (SH8601 framing: opcode 0x02 for commands,
0x32 + 0x2C00 for pixels), so no extra component is needed (the probe used `espressif/esp_lcd_sh8601`; the runtime does not). The controller wants
windows that start even and end odd, and LVGL hands over arbitrary partial areas: a full **shadow of the panel in PSRAM**, already in the panel's
byte order, takes each `hal_display_blit()` and the area is pushed from it widened to even edges, whatever the caller's alignment. Landscape is
MADCTL 0x30 and a 16-row offset. `hal_display_name()` is `RM690B0`; brightness is command 0x51.

**Input.** One side key (`hal_input_power_key()`, GPIO15, active low) and the touch panel (`hal_touch.h`). There is no wheel and no Hold switch: `hal_input_init()`
answers `HAL_ENODEV`, so the boot check marks the wheel degraded, exactly as on the RP2350 AMOLED board (touch and the console keys do not depend on it). A key press
that is already down at power-on is ignored until it has been released once. The runtime's input task owns the touch controller: it calls `hal_touch_init()` under the
I2C mutex once the bus is up and polls `hal_touch_read()` every 2 ms (`app/input/touch_gesture.c`, [Touch](touch.md)); `hal_touch.c` throttles the bus to one read per 8 ms and
uses the i2c_master driver, which locks the bus per transaction, so it shares the bus safely with `hal_i2c`. The touch HAL maps the controller's portrait coordinates to landscape
(`x = 599 - y`, `y = x`, the factory demo's mapping). **This mapping and the display image were not confirmed against a finger and by eye yet**; the three `TOUCH_*`
switches at the top of `hal_touch.c` are the one place to change the mapping.

**Radio.** `hal_wifi_present()` is false, every Wi-Fi call answers `HAL_ENODEV`, and `hal_bt_init()` answers `HAL_ENOTSUP`; the boot check reports both as
unavailable. No network is ever joined. `hal_system_late_init()` has nothing to start.

**Crypto.** `app/crypto` is compiled against IDF's mbed TLS (hardware AES/SHA); `hal_chip_id()` is the factory MAC. `hal_random()` is the hardware RNG register, which
without the radio on is a noise source rather than a certified TRNG: fine for the secrets file's per-record nonces, to be revisited when Wi-Fi/BLE are.

**Power.** `BAT_CTRL` is latched high by the first start-up init function of the image (`ESP_SYSTEM_INIT_FN` in `board.c`, before the heap and PSRAM are up), and the 0.3 s PSRAM
memory test is off, to keep the window in which a power-on key press could be released early as short as possible. `hal_system_poweroff()` drops the latch, waits 200 ms and
**returns false if the board is still running** (USB power: the charger keeps it alive; it is probed, not assumed), so the UI takes its "powering off" overlay away; on battery
the power simply goes. `hal_random()` is the hardware RNG register (not a certified TRNG while the radio is off; fine for per-record nonces; revisit with Wi-Fi/BLE).

## UI profile

The build sets `NEUPOD_UI_PROFILE=600x450` (default for this platform, `platforms/esp32s3/CMakeLists.txt`; `-DNEUPOD_UI_PROFILE=320x240` gives the small one, which then sits in
the top-left corner). The profile is a compile-time constant, wired through the IDF component as `NEUPOD_UI_PROFILE_600`. Its two partial-render buffers (600 x 30 x 2 bytes each,
72 KB together) are placed in PSRAM with the other big buffers (`ui` in `linker.lf`): they would not fit the internal RAM next to the stacks. Bench, 600x450:
boot frame 7.5 ms average, a full-pane Settings screen about 40 ms average (the pane is redrawn whole), internal heap free 76 KB after boot (32 KB at the lowest).

Measured stack minimums (bench, idle UI): `audio` 27.2 KB free of 28 KB, `vclk` 4.3 KB of 5 KB, `input` 1.8 KB of 4 KB, `storage` 9.4 KB of 16 KB, `ui` 16.5 KB of 24 KB. A decode
on the audio core has not been measured (there is no music on the bench card yet): the audio stack is 28 KB and `vclk` 5 KB so that the deeper decoder path has room; re-check
`core1_stack_free` in the monitor line the first time a FLAC and an MP3 play.

## Bench result (14 Nov 2026, 600x450 profile)

Boot log, USB console, on the board with a 32 GB FAT32 card (no music on it):

```
neuPod 0.1.0-dev (nogit) starting on esp32s3
display: RM690B0 600x450 landscape, QSPI 40 MHz, shadow 527 KB in PSRAM
boot: display   ok      : RM690B0 600x450
boot: battery   DEGRADED: gauge (0x36): no response
boot: haptics   DEGRADED: driver (0x5A): no response
boot: audio     DEGRADED: output is the null sink (no DAC output)
boot: wifi      DEGRADED: no Wi-Fi on this platform (Bluetooth only)
boot: storage   ok      : SDHC 31.2 GB, FAT32, read-only
boot: bluetooth DEGRADED: Bluetooth start failed: not supported
boot: wheel     DEGRADED: receiver: no response
touch: FT6336 chip id 0x64 (0x64 = FT6336) firmware 0x01 vendor 0x11
input: touch panel ready (600x450)
```

The UI navigates from the console keys (Home, Music, Extras, Settings), the library scan runs and finishes, and a changed setting is written to the card and read back after a
reset (`settings: loaded (7 items, seq 3)`), which proves the whole SD path: sdspi, sdmmc, `diskio.c`, FatFs and the write lock.
