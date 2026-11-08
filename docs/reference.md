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
- [ ] Confirm the polarity of the Pico Audio Pack's mute/amp-enable line (GP22) against Pimoroni's pinout.
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

## LCSC / JLCPCB availability survey (2 Nov 2026)

Live stock and JLCPCB library class for every candidate part, checked on lcsc.com and the JLCPCB parts API. Prices are JLC USD unit price at the 10-off tier; stock moves daily — re-check in the JLC BOM tool at order time. **Verdict:** all connectors, power parts and the ESP32 modules are cleanly assemblable; the CYW43439/RM2 radio, the two displays, any Qualcomm Bluetooth module and the optical jack are not, and the CS43131 is marginal.

| Function | Chosen part | LCSC # | Stock | Price @10 | JLC class | Verdict / alternative |
| --- | --- | --- | --- | --- | --- | --- |
| MCU | RP2350B QFN-80 | C42415655 | ~2–3k | ~$1.10 | Extended | OK |
| MCU alt | RP2350A QFN-60 | C42411118 | ~5k | $1.21 | Extended | OK |
| MCU alt | RP2354B (2 MB flash in-package) | C39843328 | 0–876, contradictory | $1.46 | Extended | Don't rely on it; use RP2350B + W25Q128 |
| PSRAM | APS6404L-3SQR-SN SOP-8 | C5333729 | listed 4,329 but "not available" | $2.69 | ? | Design for APS6404L-3SQR-ZR WSON-8 C3040877 (1,291, $4.20) or -3SQN-SN C3028887 (575, $5.12) |
| Flash | W25Q128JVSIQ | C97521 | ~20k | $1.66 | **Basic** | OK |
| Crystal 12 MHz 3225 | YXC X322512MSB4SI (20 pF) | C9002 | ~155k | $0.10 | **Basic** | OK; set load caps for 20 pF |
| Wi-Fi/BT | CYW43439KUBGT (bare WLBGA) | C5359496 | ~30 | $4.95 | Extended | **Not viable** at JLC; RM2 module not on LCSC (Pimoroni/Mouser, hand-place) |
| Wi-Fi/BT | ESP32-S3-WROOM-1-N16R8 | C2913202 | ~29k | $3.56 | Extended | OK — the JLC-friendly host (BLE only, no Classic BT) |
| BT classic co-processor | ESP32-WROOM-32E-N16 | C701343 | 1,533 | $3.56 | Extended | OK; A2DP source in ESP-IDF (SBC) |
| BLE/Wi-Fi 6 co-processor | ESP32-C6-MINI-1-H4 | C6553337 | 438 | $2.81 | Extended | OK (N4 variant out of stock) |
| BT audio module | Feasycom FSC-BT1026D (QCC3034) | — | not on LCSC | $6.90 direct | — | **Source direct, hand-solder**; Microchip BM64 on LCSC is 0–4 pcs |
| Antenna | YAGEO ANT3216LL00R2400A chip | C293767 | ~19k | $0.14 | Extended | OK (Johanson 2450AT18 is out of stock); FPC: Kinghelm C4943394 |
| RF π-match 0402 inductors | Sunlord SDCL1005 series | C279684 etc. | OK | — | Extended | Not Basic — 1–3 extra feeder fees |
| DAC | CS43131-CNZR | C965907 | **42** | $4.76 | Extended | Marginal — buy ahead or choose another |
| DAC alt | ES9018K2M | C2688377 | 705 | $3.05 | Extended | OK; needs HP amp |
| DAC alt | PCM5102APWR | C107671 | 2,299 | $1.30 | Extended | OK; line-level only |
| Codec with ADC | **Everest ES8311** (stereo DAC + mono ADC) | C962342 | ~100k | **$0.43** | Extended | Cheapest well-stocked codec with ADC |
| Codec with ADC | Everest ES8388 (stereo ADC/DAC + HP driver) | C365736 | ~38k | $0.94 | Extended | Best stocked full codec |
| Codec | WM8960 | C18752 | 0 | — | Extended | Unavailable |
| ADC | PCM1808PWR | C55513 | ~45k | $0.53 | Extended | OK; needs 5 V analogue |
| Haptic driver | DRV2605LDGSR | C527464 | 6,753 | $1.08 | Extended | OK |
| LRA / ERM | LEADER LD0832AA LRA / LCM1027A ERM | C2682305 / C2759984 | 1,039 / 6,892 | $1.06 / $0.45 | Extended | Lead-wire types; no pad-mount coin motor stocked |
| Piezo | Murata PKLCS1212E4001-R1 | C113159 | 7,207 | $0.74 | Extended | OK; clone C49246959 $0.36 |
| 3.5 mm TRRS jack with switch | SHOU HAN PJ-342 | C668606 | ~69k | $0.18 | Extended | Breadboard only — final board reuses the donor flex (ADR-0018) |
| Optical combo jack | Everlight PLT133/T10W | C133350 | 0 | — | — | Parked (ADR-0018); Mouser + hand-solder if it returns |
| Charger | BQ25895RTWR | C80200 | 4,599 | $1.81 | Extended | OK |
| Charger alt | BQ24074 | C54313 | 0 | — | Extended | Unavailable; MCP73871T-2CCI/ML C511310 (1,110, $1.81) is the simple alternative |
| Fuel gauge | MAX17048G+T10 | C2682616 | ~24k | $1.76 | Extended | OK (BQ27441 C473374 969 pcs, dearer) |
| Backlight boost | TPS61165DBVR / AP3031KTR | C58756 / C82636 | ~24k / ~10k | $0.64 / $0.24 | Extended | OK |
| 3.3 V buck | TPS62825DMQR | C2650334 | 4,472 | $0.42 | Extended | OK; cheap high-stock: TLV62569 C141836 $0.07 |
| Audio LDO | RT9080-33GJ5 | C841192 | ~36k | $0.12 | Extended | OK (TPS7A20 variants low stock) |
| Qi receiver | BQ51013BRHLR | C55663 | 1,066 | $3.43 | Extended | OK |
| USB ESD | USBLC6-2SC6 | C7519 | ~43k | $0.18 | Extended | OK; clones ~$0.05 |
| USB-C mid-mount 16P | SHOU HAN TYPE-C 16P CB1.6 | C2906290 | ~28k | ~$0.08 | Extended | CB1.6 is for a 1.6 mm board; CB1.0 (HCTL C2894901) exists, unverified. Top-mount fallback SHOU HAN 2MD(073) C2765186, 1.2 M stock |
| microSD push-push | Korean Hroparts TF-01A | C91145 | ~157k | $0.11 | Extended | OK, 1.85 mm high |
| FPC 0.5 mm 30P (AMOLED) | XUNPU FPC-05F-30PH20 | C2856808 | ~27k | $0.07 | Extended | OK |
| FPC 0.5 mm 50P (IPS panel) | XUNPU FPC-05F-50PH20 | C2856813 | ~11k | $0.13 | Extended | OK |
| FPC 0.3 mm 13P (wheel, if 5G-style) | HDGC 0.3K-HX-13PWB | C2919524 | not verified | ~$0.10 | Extended | 0.3/0.4/0.5 mm pitches all stocked; confirm the 6G pitch first |
| JST SH 1.0 mm 4P | SM04B-SRSS-TB | C160404 | ~54k | $0.13 | Extended | OK |
| RTC | PCF85063AT/AAZ SO-8 | C2157792 | 4,384 | $0.86 | Extended | DFN variant C404360 is out of stock |
| 32.768 kHz crystal | Seiko SC-32S 3215 | C97606 | ~153k | $0.10 | Extended | No Basic 32 kHz crystal exists |
| Schottky | SS14 | C2480 | OK | — | **Basic** | OK |
| 2.41" AMOLED RM690B0 | DisplayModule / TailorPixels | — | not on LCSC | ~$59 | — | **Source separately, fit by hand** via the 30P FPC |
| 2.4" IPS ST7789V | ER-TFT024IPS-3 (BuyDisplay) | — | not on LCSC | $6.35 | — | **Source separately** via the 50P FPC |

### JLCPCB cost model — 100 × 60 mm, 4-layer, 1.0 mm, ENIG, 5 boards, ~120 unique parts / ~250 placements

| Item | Economic PCBA (single-side only) | Standard PCBA (double-side) |
| --- | --- | --- |
| Setup | $8.18 | $25.56 / $51.12 (two sides) |
| Stencil | $1.53 | $8.21 / $16.42 |
| SMT joints (~3,750 × $0.0016) | $6 | $6 |
| Feeder fees | $3.07 per Extended part type (Basic free) → ~$169 for 55 Extended | $1.53 per part type, Basic or Extended → ~$184 |
| Bare PCB 4L ENIG (est.) | ~$25 | ~$25 |
| Components (est., ex-displays) | ~$160 | ~$160 |
| DHL to UK (est.) + 20 % VAT | ~$25 + ~$80 | ~$25 + ~$95 |
| **Landed, 5 boards** | **~$475 (≈ £360)** | **~$560 (≈ £430)** |

Setup, stencil, joint and feeder fees are verified from JLC's help pages; PCB, components and shipping are estimates. Economic is single-side only, so a double-sided layout forces Standard (+£60–90 net). The feeder fees dominate: cut unique part types, prefer Basic/Preferred values, and the price falls faster than by any other lever.

### Consequences for the design

1. **Radio:** the Pico-W-style CYW43439 cannot be built at JLC. Final-board options are an ESP32-S3-WROOM host (Wi-Fi/BLE on-module, no Classic BT) or an ESP32-WROOM-32E co-processor for classic BT, both deep-stocked; the Qualcomm audio module is a hand-soldered extra either way. An RM2 module (grant-covered) is the hand-placed alternative.
2. **Audio:** CS43131 is a 42-piece remnant. Given the recording requirement, ES8311/ES8388 are the manufacturable choices ([ADR-0012](adr/0012-audio-codec-final-board.md)).
3. **Displays** are hand-fitted parts by design; everything else on the BOM is JLC-placeable today.
4. **Mid-mount USB-C** needs the 1.0 mm (CB1.0) part verified or the board goes to 1.6 mm — which the thin shell cannot afford.
