# AMOLED 2.41 bring-up probe

Dev-only capability probe for the Waveshare RP2350-Touch-AMOLED-2.41 (RP2350B QFN80, 16 MB flash, PSRAM (8 MB part), RM690B0 450x600
AMOLED, FT6336 touch). Not part of any neuPod firmware image. One firmware, seven checks, each printing `[PASS]` or `[FAIL]` with numbers;
every wait has a timeout.

## What it does

| # | Check | How |
|---|-------|-----|
| 1 | Chip/board | GPIO25 high first thing in `main()`. Prints SDK version, ROM chip version, GPIO count (48 = B), unique ID, JEDEC flash ID and size |
| 2 | PSRAM | Own driver (`src/psram.c`) on QMI CS1 = GPIO47: reset, ID read, quad mode, QMI window-1 timing for the actual clk_sys. Prints KGD/EID, divider and PSRAM clock, size; writes and verifies a 2 MB pattern (uncached and cached); MB/s; checks real capacity by address aliasing |
| 3 | Display | RM690B0 init over PIO QSPI, landscape 600x450 test card (8 colour bars, grey ramp, 1-px orange border, "neuPod 600x450"), TE edge count, full-frame push time/fps from PSRAM and from SRAM strips, brightness 50% then 100% |
| 4 | Touch | FT6336 reset, chip/vendor/firmware ID, then polls for the rest of the run: prints landscape x,y, draws a red dot, shows x,y on the panel |
| 5 | I2C1 | Scan 0x08-0x77 (expect 0x38, 0x51, 0x6B or 0x6A), QMI8658 WHO_AM_I (0x05), PCF85063 time |
| 6 | Battery | GPIO40 (ADC0 on the B) x3 (200k/100k divider), mV, every 2 s with KEY_BAT state |
| 7 | microSD | SPI0, CMD0/CMD8/ACMD41/CMD58, then CMD9 (CSD) -> capacity. Read-only: no write or data-block read is issued |

Afterwards: touch points, battery every 2 s, summary every 20 s (so a terminal opened late still sees results). Hold Power (KEY_BAT) for
3 s to release BAT_Control (GPIO25); on battery the board switches off, on USB it keeps running and says so. The key must be seen
released once first, so the press that powers the board on is not counted.

Console keys: `m` cycles the controller's MADCTL (0x36) value and re-pushes the frame (to find out whether the RM690B0 can rotate in
hardware; the probe itself rotates in software), `s` prints the summary.

## PSRAM clock

The QMI divider is an integer, so the PSRAM clock is clk_sys / ceil(clk_sys / 109 MHz). At 150 MHz that is divider 2 = 75 MHz (no
integer divider gives anything between 75 and 150). To go faster the system clock has to rise: `-DPROBE_SYS_KHZ=216000` gives
divider 2 = 108 MHz (core voltage raised to 1.15 V; the display PIO clock and flash clock rise with it: flash 108 MHz, display SCLK
108 MHz, both unverified). UART/USB are fine either way (stdio starts after the clock change). Datasheet figures relied on, from
memory and not re-checked against the PDF: APS6404L quad read/write fast to 133 MHz (kept to 109), tCEM 8 us max, tCPH 18 ns min.
The read sample delay (rxdelay) is 1 up to 90 MHz, 2 above; only the 75 MHz setting has run on hardware.

    cmake -G Ninja -S . -B build-216 -DPROBE_SYS_KHZ=216000 && ninja -C build-216

## Landscape

The framebuffer is stored in the panel's native 450x600 portrait order; the drawing helpers map landscape (x, y) to portrait
(px = 449 - y, py = x), so landscape costs nothing at push time. Touch is mapped the same way. Whether MADCTL rotation works is
untested; use `m`.

## Pins

| Function | GPIO |
|----------|------|
| UART0 TX / RX | 0 / 1 |
| Touch INT / RST | 4 / 5 |
| I2C1 SDA / SCL (FT6336 0x38, PCF85063 0x51, QMI8658 0x6B/0x6A) | 6 / 7 |
| QMI8658 INT1 / INT2 | 8 / 3 |
| AMOLED CS / CLK / D0-D3 | 9 / 10 / 11-14 |
| AMOLED RST / TE / PWR_EN | 15 / 16 / 17 |
| microSD SCK / MOSI / MISO / CS (SPI0) | 18 / 19 / 20 / 23 |
| KEY_BAT (active low) / BAT_Control (soft latch, high = stay on) | 24 / 25 |
| BAT_ADC (ADC0) | 40 |
| PSRAM CS1 | 47 |

GPIO17: the schematic says PWR_EN; the Waveshare C demo disabled it (`PIN_PWR_EN -1`), the Arduino one uses 17. The probe drives 17 high.

## Build

Needs the Pico SDK 2.3.x (`PICO_SDK_PATH`, or `~/pico/pico-sdk`), `arm-none-eabi-gcc`, cmake >= 3.27, ninja, picotool.

    cd testbed/amoled241/bringup
    export PICO_SDK_PATH=$HOME/pico/pico-sdk
    cmake -G Ninja -S . -B build
    ninja -C build

Output: `build/amoled241_bringup.uf2` and `.elf`. Board header: `boards/waveshare_rp2350_touch_amoled_2.41.h` (RP2350B, 16 MB
flash, W25Q080 boot2). Stdio is on USB CDC and UART0 (115200); the SDK's USB reset interface is on, so `picotool -f` works.

## Flash

    picotool load -f -x build/amoled241_bringup.uf2

(or hold BOOT while plugging in USB and copy the UF2 to the RPI-RP2 drive). Open the USB serial port; the probe waits up to 6 s for a terminal.

## Restore the factory demo

Download `RP2350-Touch-AMOLED-2.41.zip` (the demo package) from the Waveshare wiki page for the board, unzip, and flash the
prebuilt UF2 from `C/uf2/`:

    picotool load -f -x C/uf2/02-FatFs.uf2

(`-f` reboots a running application into BOOTSEL first; this probe has the USB reset interface, the factory demo may not: if
`picotool` cannot find the board, hold BOOT while connecting USB and copy the UF2 to the drive.)

## Not verified without hardware

Everything runtime. It builds; nothing has been run. See the report that came with this commit for the specific doubts.
