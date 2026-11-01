# Reference

Exported from the neuPod design doc (Reference tab). Working list for the iPod Classic replacement-board build. Prices are approximate UK/low-volume, October 2026. Add to it as things are ordered or ruled out.

## Goal and scope

A working list of suppliers, parts, upgrades and technologies for the build — the shopping and
sourcing side of the [Requirements](requirements.md) and [Design](design.md). Prices are
approximate UK low-volume figures in GBP unless marked; some overseas suppliers quote in USD.

## Target hardware

### Phase 1 — Pico 2 W prototype rig

Plan: a Raspberry Pi Pico 2 W (or Pimoroni Pico Plus 2 W) on a breadboard/perfboard, wired to the donor's click wheel, LCD, headphone flex and battery via FPC breakout boards, to develop the drivers and UI before any custom PCB.

| Item | Why | Source | ~Cost |
| --- | --- | --- | --- |
| Raspberry Pi Pico 2 W | RP2350A, 4 MB flash, CYW43439 Wi-Fi/BT, 26 GPIO. The baseline. | Pimoroni, The Pi Hut, Farnell, RS | £6–7 |
| Pimoroni Pico Plus 2 W | RP2350B (48 GPIO), 16 MB flash, 8 MB PSRAM, same CYW43439 radio, USB-C. Closer to the final board — PSRAM for album art and audio buffers, more pins for a 16-bit LCD bus. **Preferred dev board.** | Pimoroni | ~£17 |
| Pico Debug Probe | SWD debugging + UART; essential once drivers get involved | Pimoroni, Pi Hut | ~£12 |
| FPC/FFC breakout boards, 0.5 mm pitch, 50-pin (for the ER-TFT024IPS-3) | Brings the panel out to 2.54 mm headers | eBay / AliExpress ("FPC 0.5mm 50 pin adapter board") | £2–4 |
| FPC breakout boards for the iPod click wheel, LCD and headphone flexes | Pitch/pin count to be measured on the donor; buy a 0.3/0.4/0.5 mm assortment | eBay / AliExpress | £5 for a mixed pack |
| Second donor Classic or a spare click wheel + headphone flex | Spares for the inevitable torn ribbon | eBay UK, Elite Obsolete | £10–30 |
| I²S DAC breakout (PCM5102A) | Stand-in for the CS43131 until the custom board; proves the audio pipeline | Pimoroni (Pico Audio Pack), Pi Hut, eBay | £4–15 |
| microSD breakout (SPI/SDIO) | Storage during prototyping | Adafruit via Pi Hut, eBay | £3–5 |
| 1,000–2,000 mAh LiPo with JST-PH + LiPo SHIM / charger board | Battery power on the bench | Pimoroni (LiPo SHIM for Pico), Pi Hut | £8–15 |
| Logic analyser (Saleae-compatible 8ch or DSLogic) | Sniffing the stock board's click-wheel serial line and LCD bus before it's retired | eBay, Pi Hut | £10–60 |

**Tip:** before removing the original logic board, probe the wheel and LCD ribbons live to confirm Rockbox's pinouts against this exact board revision.

## Compute options

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| MCU | RP2350B (48 GPIO) or RP2354B (2 MB flash in-package) | QFN-80 | Farnell, Mouser, DigiKey, Pi Hut | £1–1.50 |
| PSRAM | APS6404L-3SQR 8 MB QSPI (as Pico Plus 2 W) | SOP-8 | Mouser, LCSC | £1–2 |
| Flash | W25Q128JV 16 MB QSPI | SOIC-8 | LCSC, Mouser | £1 |

Platform comparison and the RP2350-vs-ESP32-S3 trade-off: [Design → Compute options](design.md#compute-options).

## Connectivity

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| Wi-Fi/BT (option A) | Infineon CYW43439 — Pico W reference design, needs BLE/BR-EDR firmware blob and antenna | WLBGA | Mouser (restricted); easier as a pre-made module (e.g. Pico W module footprint) | £3–5 |
| Wi-Fi/BT (option B) | ESP32-C6-MINI-1 module as radio co-processor over SPI (BLE 5 + Wi-Fi 6; **no classic BT/A2DP** — see note) | Module | Mouser, LCSC, Pi Hut | £2–3 |
| Wi-Fi/BT (option C) | ESP32-WROOM-32E module (Xtensa, classic BT A2DP source + Wi-Fi 4) as co-processor | Module | LCSC, Mouser | £2–3 |
| USB-C | mid-mount 16-pin receptacle, 5.1 kΩ CC pulldowns, ESD (USBLC6-2) | — | LCSC, Mouser | £0.50 |
| Antenna | 2.4 GHz chip (Johanson 2450AT18) or FPC antenna | — | Mouser, eBay | £1–2 |

**Radio note:** AirPods and most headphones use classic Bluetooth A2DP. CYW43439 (Pico W) supports BR/EDR and BTstack ships with an A2DP source profile, which is why Sprout uses it. ESP32-C6/S3 are BLE-only; the original ESP32 (option C) does A2DP source. Decide this before the first board.

## Storage

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| microSD socket | push-push, SDIO 4-bit | — | LCSC, Mouser | £0.50–1 |

## Display

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| Backlight driver | small boost / constant-current LED driver for 4×WLED @ 70 mA (e.g. TPS61165 / AP3031) | SOT | Mouser, LCSC | £0.50–1 |
| FPC/ZIF connectors | 50-pin 0.5 mm (panel), plus wheel/headphone/battery connectors matched to donor ribbons | — | LCSC, Mouser | £2–3 |

Panel candidates and specifications: [Requirements → Display](requirements.md#display).

## Audio

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| DAC + HP amp | Cirrus CS43131 (Sprout's choice) or ESS ES9018K2M + amp | QFN | Mouser, DigiKey | £5–8 |
| Haptics (optional) | coin LRA + DRV2605L driver | — | Mouser, Pi Hut | £3–5 |

## Power and battery

| Function | Part | Package | Supplier | ~Cost |
| --- | --- | --- | --- | --- |
| Charger + power path | TI BQ25895 (I²C, OTG) or BQ24074 (simple) or MCP73871 | QFN/VQFN | Mouser, Farnell | £2–4 |
| Fuel gauge | TI BQ27441-G1 or Maxim MAX17048 | DSBGA / TDFN | Mouser | £2–3 |
| Qi receiver (optional) | TI BQ51013B (5 V out) or BQ51050B (direct charge); 43×28 mm receiver coil with ferrite | QFN | Mouser; coils from Würth/TDK via Mouser or eBay | £3–5 + £3 |
| Buck/LDO | 3.3 V buck (TPS62xxx) + low-noise LDO for DAC analogue | — | Mouser, LCSC | £1–2 |

### Batteries

| Use | Part | Notes |
| --- | --- | --- |
| Bench | 1,000–2,000 mAh LiPo, JST-PH, with PCM | Pimoroni / Pi Hut |
| Thin shell final | 405080 (~2,000 mAh, 4 mm) or 4055100-class | eBay / AliExpress pouch cells with PCM; check real thickness |
| Thick shell final | 605070 / 605080 (~3,000 mAh, 6 mm) | Same |
| Drop-in (stock board) | iFlash / Moonlit / Elite Obsolete 3-pin iPod cells, 1,000–3,000 mAh | Only if keeping the stock connector |

## Reused parts and mechanical

### iPod parts and mod suppliers

| Supplier | Location | What | Notes |
| --- | --- | --- | --- |
| [iFlash](https://www.iflash.xyz) | UK | SD/mSATA adapters (Solo, Quad, Dual, Sata), batteries, shells, tools, compatibility tables | The storage standard; good install docs |
| Moonlit Market | UK | Classic Connect 2 kit (BT 5.2 aptX, Qi coil, haptic, USB-C, frosted plastic back, 3,000 mAh), backs, batteries | Only source of a ready-made Qi-compatible back |
| Elite Obsolete | US | Transparent/coloured shells, click wheels, faceplates, backs, batteries, build bundles | Boutique; dearer postage |
| Boxy Pixel | US | CNC aluminium + glass shell kit with BT/USB-C/Qi board ($155 + $13) | Reference for a glass-backed Qi design |
| BuyDisplay / EastRising | CN (eBay store) | ER-TFT024IPS-3 2.4" IPS panel, module version, ZIF connectors | See Display section of the requirements |
| eBay UK sellers (e.g. item 265079127944) | UK | Back covers £10–12, thin and thick | Also opening tools (ebuyergate, Bradford) |
| thepartguru / ResaleFirm | CA / US | Thick and thin backs, fronts, wheels | Better QC reviews, higher postage |
| iFixit | US/EU | Opening tools, batteries, guides | Guide #572 for the Classic |

### Mechanical

- Back cover: thick steel back (eBay, ~£10) for battery depth; plastic/glass back needed if Qi is fitted (Moonlit kit or own 3D print / CNC polycarbonate).
- USB-C filler for the 30-pin aperture: 3D print, or CNC from the back-cover material.
- LCD bezel/mask for the 2.4" panel in the 2.5" window: thin printed frame or black PET gasket.
- Mid-frame: reuse; design board to its screw holes.
- 3D models: ER-TFT024IPS-3 STEP from BuyDisplay; Pico 2 W STEP from Raspberry Pi; community iPod Classic shell models on Printables/Thingiverse (verify against the donor).

## Firmware

### Firmware, OS and software stack

| Layer | Choice | Notes |
| --- | --- | --- |
| SDK | Raspberry Pi Pico SDK (C/C++), CMake, arm-none-eabi | Primary. MicroPython for quick pin-poking experiments only |
| RTOS | FreeRTOS (SMP on RP2350) or bare-metal with the SDK's multicore | Audio decode on core 1, UI/IO on core 0 |
| Wi-Fi/BT | cyw43-driver + lwIP (Wi-Fi) and BTstack (BLE + classic, A2DP source) — all bundled with the Pico SDK for Pico W/2 W | Sprout uses the same stack |
| USB | TinyUSB (bundled): mass storage for drag-and-drop, UAC2 for USB-C audio out, CDC for debug | — |
| Filesystem | FatFs (exFAT enabled) over SDIO via PIO | — |
| Codecs | minimp3 or libhelix-mp3; libhelix-aac / FDK-AAC; dr_flac; Apple ALAC decoder (Apache 2.0); libopus (fixed-point build); stb_vorbis | All proven on Cortex-M4/M33 |
| UI | LVGL 9 (with ST7789 driver) or a hand-rolled iPod-style menu renderer | LVGL gets Cover Flow-style animations cheaply |
| Display driver | ST7789V via PIO 8080-16 + DMA (new panel); Rockbox `lcd-ipod6g.c` port for the stock panel | — |
| Click wheel | Port of Rockbox `button-clickwheel-6g` serial decoding | Protocol known |
| Fonts/icons | LVGL font converter; original look needs an iPod-like sans (Chicago-style bitmap alternatives are freely licensed) | — |
| Reference firmware | Sprout (github.com/seanpod/sprout, MPL-2.0, pending release); Rockbox (GPL, ipod6g target); sPot (Guy Dupont, GitHub) | Rockbox has no Cortex-M port — reference only unless you port it |
| Tools | OpenOCD / picotool, Pico Debug Probe; Saleae/Sigrok for bus capture | — |
| C# angle | .NET nanoFramework has community RP2040 support but not RP2350 yet; not viable for audio. Host-side tooling (library sync app, firmware updater) is the natural place for C# | — |

## Cost

### PCB fabrication and assembly

| Vendor | Use | Notes |
| --- | --- | --- |
| JLCPCB | 4-layer boards, SMT assembly from LCSC stock | 5× 4-layer ~£20–40; assembly £5–15/board; check LCSC stock before choosing parts |
| PCBWay | Alternative, better for odd stack-ups | — |
| Aisler (DE) | EU-made, fast, no customs | Dearer per board |
| LCSC | Passives, connectors, Chinese-sourced ICs | Pairs with JLC assembly |
| Mouser UK / Farnell / RS / DigiKey | TI, Cirrus, Infineon parts; next-day in the UK | — |
| KiCad 9 | EDA; has RP2350 symbols/footprints via Raspberry Pi's KiCad library | Free |

## Prior art

### Communities and documentation

- Rockbox wiki: IpodClassic, IpodClassicHardware, DeviceChart; source on GitHub `Rockbox/rockbox`
- Raspberry Pi: RP2350 datasheet, Pico 2 W datasheet and schematic (CYW43439 reference layout), Hardware design with RP2350 guide
- BTstack docs (A2DP source example `a2dp_source_demo.c`)
- r/ipod wiki and build threads; iFlash forums
- Hackaday: sPot write-up; Hackster: Sprout coverage
- YouTube: DankPods, Zac Builds, Macho Nacho Productions (Moonlit and Boxy Pixel installs)
- iFixit guide #572 and the iPod Classic teardown

## Open items

- [ ] Check LCSC/JLCPCB stock for every final-board part before locking the BOM (RP2350B is
  JLCPCB part C42415655; RP2350A is C42411118).
- [ ] Confirm the Pico Audio Pack's mute/amp-enable pin and polarity against its silkscreen.
- [ ] Order FPC breakouts once the donor's connector pitches are measured.
- [ ] Source a QCC3008-class Bluetooth transmitter board for the A2DP bench comparison.

## Upgrades considered (keep / drop)

| Upgrade | Status | Notes |
| --- | --- | --- |
| 2.4" IPS panel (ER-TFT024IPS-3) | Keep — primary | Stock panel retained as plan B |
| Colour OLED | Dropped | Nothing practical at 2.5" 320×240 |
| Mono OLED | Dropped | No album art |
| Bluetooth A2DP | Keep | Drives the radio choice |
| Wi-Fi | Keep | Podcasts, NAS sync, OTA; streaming needs a Linux module |
| USB-C data + charge | Keep | Mid-mount through 30-pin aperture |
| Qi wireless charging | Optional | Needs non-metal back |
| Haptic wheel | Optional | LRA + DRV2605L |
| Linux module (CM Zero / ESP32-P4) footprint | Optional, later | Streaming apps |
| iFlash + Rockbox on stock board | Reference only | Fallback if the custom board stalls |
