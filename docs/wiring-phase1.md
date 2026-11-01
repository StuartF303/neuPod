# Phase 1 wiring (Pico 2 W testbed)

Final pin plan for the Phase 1 breadboard testbed. Pin numbers are RP2350 GPIOs (GPn).

## Debug console

| Signal | GPIO | Connects to |
|--------|------|-------------|
| UART0 TX | GP0 | Debug Probe UART RX |
| UART0 RX | GP1 | Debug Probe UART TX |

## microSD (SDIO 4-bit, PIO)

| Signal | GPIO |
|--------|------|
| CLK | GP2 |
| CMD | GP3 |
| DAT0 | GP4 |
| DAT1 | GP5 |
| DAT2 | GP6 |
| DAT3 | GP7 |

## Display — Adafruit 2.0" 320×240 IPS TFT (ST7789, SPI1, landscape)

| Signal | GPIO | Notes |
|--------|------|-------|
| Backlight (PWM) | GP8 | |
| SPI1 MISO | GP12 | |
| TFT CS | GP13 | |
| SPI1 SCK | GP14 | |
| SPI1 MOSI | GP15 | |
| D/C | GP16 | |
| RST | GP17 | |
| SD_CS (breakout's own card slot) | GP18 | unused |

## Audio — Pimoroni Pico Audio Pack (I²S)

| Signal | GPIO |
|--------|------|
| I²S DATA | GP9 |
| I²S BCK | GP10 |
| I²S LRCK | GP11 |
| Mute / amp enable | GP22 |

## I²C0

| Signal | GPIO |
|--------|------|
| SDA | GP20 |
| SCL | GP21 |

| Device | Address |
|--------|---------|
| MAX17048 fuel gauge | 0x36 |
| DRV2605L haptic driver | 0x5A |

## Click wheel (reserved)

| GPIO | Wheel line |
|------|------------|
| GP19 | TBC |
| GP26 | TBC |
| GP27 | TBC |
| GP28 | TBC |

The wheel has four lines (CLK, SDO, SDI and one more, TBC). Which line goes to which reserved
GPIO is not yet decided. See [click-wheel protocol](click-wheel-protocol.md).
