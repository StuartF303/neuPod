# Requirements

Exported from the neuPod design doc (Requirements tab, 25 Oct – 1 Nov 2026). Prices are approximate UK low-volume figures. The supplier and parts list is on the [Reference](reference.md) page; the engineering detail is on the [Design](design.md) page.

## Goal and scope

Design a drop-in replacement logic board for the iPod Classic 6G/7G that adds Wi-Fi, Bluetooth audio, microSD storage and USB-C, while reusing the original shell, click wheel and (optionally) screen.

- Fully reversible: original board, drive and battery can go back in.
- No soldering to original iPod parts; board connects to the existing flex cables.
- Fills a gap: no existing product gives a Classic Wi-Fi (Sprout targets 3G/4G/mini only; Moonlit, Boxy Pixel and iFlash add BT/USB-C/storage but keep the stock SoC).
- Target sizes: thin shell first (the donor in hand), thick shell as the easier variant.

## Target hardware

Donor in hand: Apple iPod Classic 6th generation, model A1238, EMC 2173, 120 GB — the **thin** shell (bought on eBay, 25 Oct 2026).

| Item | Thin Classic (120 GB, late 160 GB) | Thick Classic (80 GB, early 160 GB) |
| --- | --- | --- |
| Overall thickness | ~10.5 mm | ~13.5 mm |
| Internal depth (approx) | ~8 mm | ~11 mm |
| Stock battery | ~550–650 mAh | ~850 mAh |
| Stock HDD | 5 mm, 1.8" ZIF | 8 mm, 1.8" ZIF |
| Opening | Spring clips, stiffer | Spring clips |

Why 6G/7G over 4G/5G: cheapest donors (£20–40 dead-HDD units), 320×240 colour panel, bigger wheel, and the LCD/wheel were reverse-engineered by Rockbox. 6G vs 7G makes no difference to a replacement board (same shell, screen, wheel). A thin-to-thick rear-shell swap (~£10) is a cheap way to gain battery depth.

Opening: iFixit guide [iPod Classic Logic Board Replacement #572](https://www.ifixit.com/Guide/iPod+Classic+Logic+Board+Replacement/572). Thin metal opening tools; release clips along both long edges; open like a book hinged at the bottom-left; disconnect battery flex (bottom) and headphone/hold flex (top, ZIF) before separating; HDD on a ZIF ribbon — flip the latch before pulling.

## Compute options

Current preference: **RP2350B** as the always-on brain (Sprout Pico approach), with a footprint reserved for a Linux module (Raspberry Pi Compute Module Zero or ESP32-P4) if streaming apps are wanted later.

Raspberry Pi's Broadcom application processors (BCM2710/2711/2712, the Zero 2 W's RP3A0 SiP) are not sold to third parties, so only their microcontrollers (RP2040/RP2350) can go on a custom PCB.

| Option | Cores / clock | RAM | Wi-Fi/BT | Linux | PCB layers | Unit cost | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RP2350B | 2× Cortex-M33 (or RISC-V) @ 150 MHz | 520 KB SRAM + QSPI PSRAM (up to 8 MB) | External | No | 4 | ~£1 | 48 GPIO; RP2354 variants have 2 MB flash in-package; instant-on; ~40 h battery class; PIO+DMA drives parallel LCD |
| RP2040 | 2× Cortex-M0+ @ 133 MHz | 264 KB | External | No | 4 | ~£0.70 | Too little RAM/CPU for colour UI + codecs; avoid |
| ESP32-S3 | 2× Xtensa LX7 @ 240 MHz | 512 KB + 8 MB PSRAM | On-chip (2.4 GHz n, BLE 5) | No | 4 | ~£2–3 | Simplest single-chip Wi-Fi/BT route; classic BT A2DP needs the older ESP32, S3 is BLE-only (LE Audio) |
| Allwinner T113-S3 | 2× Cortex-A7 @ 1.2 GHz | 128 MB DDR3 in-package | External module | Mainline | 4 | ~£4–5 | QFN128, hand-reworkable; best fully-custom Linux route |
| Rockchip RV1106 | Cortex-A7 @ 1.2 GHz | 256 MB in-package | External | Yes | 4 | ~£4–6 | Luckfox Pico reference design |
| Zero 2 W as module | 4× Cortex-A53 @ 1 GHz (RP3A0) | 512 MB LPDDR2 | CYW43438 on-board | Yes | Carrier only | ~£14 | 65×30×~1 mm; 6-layer board; soldered to a custom carrier; 0.3–0.6 W idle |
| Compute Module Zero | as Zero 2 W | 512 MB | On module | Yes | Carrier only | ~£15–20 | What Sprout Pi uses; check availability |

Avoid any SoC needing external DDR (6–8 layers, impedance control).

## Connectivity

Required: 2.4 GHz Wi-Fi, Bluetooth audio (A2DP to AirPods/headphones/speakers), USB-C for charging and drag-and-drop file transfer (mass storage), digital audio out over USB-C.

- **Radio for RP2350 route:** Infineon CYW43439 over SPI (Pico W / Pico 2 W reference design is open, ~£2–3 + antenna) or an ESP32-C6 as radio co-processor over SPI (~£1.50, Wi-Fi 6 + BLE 5 only, easier to source). The C6 has **no classic Bluetooth, so no A2DP**; it only suits Wi-Fi.
- **Classic Bluetooth A2DP** is needed for most headphones; confirm the chosen radio supports BR/EDR, not just BLE.
- **Antenna (requirement, 3 Nov 2026):** the shell is a Faraday cage; the only RF-transparent area is the **top plastic window of the 6G back**. The antenna sits there with its keep-out, and the radio module chosen must have a u.FL connector or an on-module antenna that can be placed there. Applies to Bluetooth first.
- **Final-board radio:** not a bare CYW43439 (not assemblable at JLCPCB, outside the BTstack grant). Options: RM2 module, or an ESP32 with its own stack — see [ADR-0007](adr/0007-bluetooth-audio-path.md).
- **USB-C:** 5.1 kΩ CC pulldowns for 5 V from C-to-C cables; USB 2.0 data; no PD needed at 1–2 A. The 30-pin aperture (~21 × 3 mm) takes a mid-mount USB-C receptacle with a small printed/CNC filler.
- **GPIO:** 2× spare via the wired-remote connector (as Sprout) is a nice-to-have.

Existing kits for reference: Moonlit Market Classic Connect 2 (BT 5.2 aptX, Qi, haptics, USB-C charge; sync needs soldering), Boxy Pixel (BT 5.0 + USB-C charge only).

## Storage

microSD, SDXC up to 2 TB, via SDIO (4-bit) for throughput; exFAT so the card is readable on a PC.

- Push-push or hinged socket, accessible without opening the case is not possible on a Classic, so a reliable internal socket is enough.
- Keep a 2 TB card on the test list: controller compatibility is where iFlash users hit trouble.
- Library index/metadata cached in PSRAM or on-card database to keep browse fast with 100k+ tracks.
- **Card power (requirement, 3 Nov 2026):** GPIO-controlled load switch on card VDD, so a wedged card can be power-cycled. SD clock from integer dividers only; multi-block reads capped at 128 blocks ([Storage](storage.md)).

## Display

Preferred upgrade: a 2.4"–2.8" IPS TFT (ST7789V / ILI9341, 240×320) replacing the stock panel; keep the stock panel as plan B.

| Option | Size / resolution | Interface | Cost | Pros | Cons |
| --- | --- | --- | --- | --- | --- |
| Stock 6G/7G panel | 2.5", 320×240 TN | 8080-style parallel (init sequence in Rockbox `ipod6g`) | £0 (reused) | No mechanical change; fully reversible | Dim, low contrast; must be driven exactly as Rockbox does |
| IPS TFT, ST7789V / ILI9341 | 2.4" (~60 × 42 mm active) to 2.8", 240×320 | SPI or 8080 parallel; RP2350 PIO+DMA | £3–6 bare panel | Near-OLED contrast; trivial to drive; what Zac Builds used | Needs masking/bezel fit to the Classic window; new FPC routing |
| Mono OLED (SSD1309 / SSD1322) | 2.4–2.7", 128×64 or 256×64 | SPI / 8080 | £5–15 | Retro Sprout look; very low power | No album art, no colour |
| Colour OLED | ≤1.5" at 128×128; larger are MIPI phone panels | SPI, or MIPI needing a bridge | varies | — | Nothing practical at 2.5" 320×240; rule out |

Requirement: measure the Classic's window aperture and the LCD bay in the mid-frame before choosing a panel part number.

**Shortlisted panel: EastRising [ER-TFT024IPS-3](https://www.buydisplay.com/2-4-inch-ips-240x320-tft-lcd-display-capacitive-touch-screen)** — 2.4" IPS 240×320, ST7789V, bare glass, 50-pin 0.5 mm FPC. Active 36.72 × 48.96 mm, visual 38.72 × 50.96 mm, outline 42.72 × 59.26 × 2.3 mm (stock Classic panel ≈ 38 × 51 mm active, so ~1 mm margin each edge). 8080 8/9/16/18-bit, SPI, RGB; 500:1, 300 cd/m², 80° all round; VDD 2.8 V typ, backlight 4 WLED 70 mA. $6.35 direct; module version ER-TFTM024IPS-3 on [eBay (BuyDisplay)](https://www.ebay.co.uk/itm/305668452892) ~$12.60 for prototyping. Datasheet, STEP model, ST7789V datasheet and ZIF drawing on the product page. Alternative: Ronbo RB024D12N19B (same glass, 12-pin SPI-only FPC).

**Premium option: 2.4" AMOLED 450×600, Raydium RM690B0** (DisplayModule [DM-AMOLED240-011](https://www.displaymodule.com/products/2-4-amoled-display-450x600-spi-mcu-mipi), also Anders, TailorPixels, SWICN; ~£45–50 single, ~$25 Alibaba lots). Same 36.72 × 48.96 mm active area as the IPS panel so it fits the window identically; 311 ppi, 24-bit, 100,000:1, 60 Hz; QSPI/SPI/MIPI. Needs an AMOLED PMIC (TPS65632-class, AVDD ~4.6 V / ELVSS negative) and a QSPI write path (PIO on RP2350 at reduced frame rate, native on ESP32-S3). Burn-in and content-dependent power are the trade-offs. Ruled out: no OLED exists in the Classic's native 2.5" 4:3 landscape; memory-in-pixel, e-paper, micro-LED and phone MIPI AMOLEDs don't suit. Final board could carry both the 50-pin IPS and the AMOLED FPC footprints.

## Audio

Hi-fi DAC with headphone amp driving the original 3.5 mm jack via the existing headphone/hold-switch flex, plus Bluetooth A2DP and USB-C digital audio out.

- Codec (3 Nov 2026, [ADR-0012](adr/0012-audio-codec-final-board.md), proposed): **ES8311** baseline, **ES8388** alternative, **PCM5102A** as the DAC-only no-MCLK fallback. CS43131 dropped (≈42 pcs at LCSC). Open point: the ES8311 is mono.
- I²S from the MCU; the board provides MCLK (256 fs from `clk_gpout`), but the codec must also run with its PLL from BCLK.
- **Audio isolation (requirement):** audio runs on a dedicated bare-metal core with lock-free rings only ([ADR-0011](adr/0011-bare-metal-audio-core.md)); any SoC candidate needs a spare core.
- Codecs required: MP3, AAC, ALAC, FLAC, Opus (software decode on RP2350 is fine at 150 MHz).
- Optional: small LRA for haptic click-wheel feedback (~£1, as Moonlit's kit).

**Single jack, line or headphone:** one 3.5 mm output from the DAC's headphone driver serves both; the codec's load/jack detection (CS43131 had this; check the ADR-0012 part) can switch a firmware "line mode" (fixed level, volume bypassed) when a high-impedance load is seen. No separate line-out socket.

**Optional: optical S/PDIF via a mini-TOSLINK combo jack** (Apple-style). Parts: 3.5 mm optical-combo socket with integrated IR transmitter (Everlight PLT133/T10W-series, Cliff FCR684206 or similar, ~£2–4; check barrel depth against the Classic's jack aperture). Signal generated by an RP2350 PIO state machine (biphase-mark, 64× fs) from the same audio buffer as I²S; route to S/PDIF and bypass volume when an optical plug is detected. The jack then lives on the new board rather than on the original headphone flex (flex still used for the hold switch).

**Audio quality policy:** lossless end-to-end on every wired/digital path. Decode once to a 32-bit mixer; no resampling unless the sink requires it; bit-perfect FLAC/ALAC to the DAC (CS43131: 32/384, ~130 dB DNR), to optical S/PDIF (24/96) and to USB-C UAC2. Dedicated low-noise analogue LDO and a 22.5792/24.576 MHz clock pair (or DAC PLL) for the DAC. Decode target: 24/96 FLAC on RP2350 (24/192 only on ESP32-S3-class).

| Bluetooth codec | Max bitrate | Available on CYW43439 + BTstack | Available on an external QCC module |
| --- | --- | --- | --- |
| SBC / SBC-XQ | 328k / ~500k | Yes | Yes |
| AAC 256k | 256k | Encoder not free (FDK licence, heavy on M33) | Yes |
| aptX / aptX HD | 352k / 576k | No (Qualcomm licence) | Yes |
| LDAC | 990k | No (Sony licence) | Some modules |
| LC3 (LE Audio) | ~345k | Immature | Varies |

**Decision pending:** Bluetooth path = SBC-XQ on the CYW43439 (simplest, no extra parts) or an external Qualcomm QCC3008/3040/5125 transmitter module on I²S with UART control (aptX HD/AAC/LDAC, what the commercial iPod BT kits use). Wi-Fi/BT coexistence and the metal shell limit link quality either way.

**Recording (new requirement, 31 Oct 2026):** the board shall record audio to microSD. Sources, in priority order: (1) mono mic on a 4-pole headset plugged into the 3.5 mm jack; (2) Bluetooth headset mic (HFP, voice quality only); (3) optional on-board MEMS mic for voice notes; (4) USB-C audio in. Target formats: WAV 48 kHz/16-bit for the jack and MEMS paths; whatever the BT profile delivers for HFP. The codec/DAC chosen must therefore include an ADC input (CS43131 is DAC-only — either add a small ADC such as a PCM1808/ES8311/WM8960-class codec, or move to a combined codec part). Keep the ADC analogue front end on its own clean LDO, away from the switcher, same as the DAC.

**3.5 mm jack = 4-pole TRRS, CTIA pinout** (tip L, ring 1 R, ring 2 GND, sleeve mic). Requirements:

- Mic bias ~2.0–2.7 V via ~2.2 kΩ, switchable off in firmware.
- Inline-button decode: headsets short the mic line through resistors (play/pause ≈ 0 Ω, vol+ ≈ 240 Ω, vol− ≈ 600 Ω); read with an ADC channel on the mic line and map to click-wheel actions.
- Jack insert detect (switched contact on the socket or codec jack-detect) to route headphone/line/optical and to disable mic bias when a 3-pole TRS plug is present (its sleeve is ground).
- Design for CTIA only; OMTP (mic/GND swapped) headsets are not supported.
- The optical-combo socket option above must still be 4-pole if chosen; check availability (optical + TRRS combos are rarer than optical + TRS).

**Bluetooth profiles required:** A2DP **source** for stereo playback — neuPod sends audio to the headphones/speaker, which are the A2DP sink (SBC/AAC minimum; aptX/LDAC per the transmitter decision above). **AVRCP** (neuPod as target, the headset as controller) for play/pause/next/previous from the headset buttons, plus **absolute volume** so the headset's volume and neuPod's are one control. HFP in the Audio Gateway role for the headset mic — note it drops both directions to 8/16 kHz mono (CVSD/mSBC), so BT recording is voice-note quality only. LE Audio / LC3 (stereo, bidirectional, good quality) is a nice-to-have: pick a radio whose stack can do LE Audio so it can be enabled later. Reference headset for testing: Stuart's AirPods Pro 3 (model A3063/A3064, 2025, Apple H2) — A2DP + AAC for playback, HFP for mic; LE Audio only via Apple's own path, so not usable for LC3 testing.

## Power and battery

Target: 2,000+ mAh in the thin shell, 3,000 mAh in the thick shell; 40 h Bluetooth playback is the benchmark (Sprout's figure with 2,500 mAh).

| Shell | Cell thickness available | Candidate cells | Capacity |
| --- | --- | --- | --- |
| Thin (donor) | ~4 mm once HDD + original board are gone (~1 mm board) | 405080, 4055100-class pouch | ~2,000–2,500 mAh |
| Thick | ~6–7 mm | 605070 / 605080, 7 mm variants | ~3,000–3,500 mAh |
| Stock reference | thin 550–650 mAh, thick 850 mAh; aftermarket thin ~1,000–1,200 mAh, thick 3,000 mAh (with stock board + iFlash) |  |  |

- Cells with built-in PCM (OV/UV/OC protection); thermistor input on the board.
- Charger (3 Nov 2026): **BQ25895** (I²C 0x6A, INT pin, full status registers, OTG, power-path). Firmware models its state machine in `hal_charger.h`; the testbed (Micro-Lipo MCP73831, no STAT pin) is a second implementation: VBUS present (cyw43 `WL_GPIO2`) AND MAX17048 CRATE > 0 = charging; VBUS AND CRATE ≈ 0 AND SOC ≥ 98 % = full.
- Fuel gauge for real percentage in the UI: BQ27441 or MAX17048.
- Rails: 3.3 V for MCU/radio/SD, clean analogue rail for the DAC, LCD backlight boost.
- Power states: instant-on from sleep (RP2350 dormant mode), deep sleep when hold is on, hard off.

**Power budget by state (estimates, to be measured on the bench):**

| Block | Standby | Playing, screen off, BT |
| --- | --- | --- |
| RP2350 core | ~1 mA sleep / 0.2 mA dormant | 25–40 mA |
| ESP32-S3 core + octal PSRAM | ~10 µA deep sleep / ~2 mA light | 60–100 mA |
| CYW43439 BT A2DP | <1 mA | 30–50 mA |
| QCC3008-class BT module | ~1 mA | 20–35 mA |
| microSD | <0.2 mA | ~10 mA average (bursts to 60) |
| CS43131 DAC + HP amp | off | 10–25 mA |
| 2.4" IPS + backlight | off | 100–150 mA when on |
| 2.4" AMOLED | off | 40–200 mA when on, content dependent |
| Regulators, gauge, misc | ~1 mA | ~5 mA |

Totals playing to BT, screen off: architecture A (RP2350 + CYW43439) ~80–110 mA → 23–30 h on 2,500 mAh; architecture B (ESP32-S3 + QCC) ~110–160 mA → 16–22 h. Standby target <0.5 mA: load switches on SD, DAC and radio rails; wake from hold switch or wheel. Backlight dims to ~10% after the idle time (default 30 s) and goes fully off 3 min after that (Settings > Display, both adjustable; [ADR-0020](adr/0020-hold.md); this replaces the earlier "off within ~15 s": the totals above assume the screen off, so a listening session pays the 3.5 minutes of display once, after each press); Wi-Fi off during playback; core clock scaled to the minimum that keeps the audio buffer full.

## Reused parts and mechanical

Reuse the shell, mid-frame, click wheel and its FPC, headphone/hold-switch flex, and (plan B) the LCD. Everything connects via the original ribbons into FPC/ZIF connectors on the new board.

**Known internal geometry (open sources; everything else comes from the donor scan):** envelope 103.5 × 61.8 × 10.5 mm thin / 13.5 mm thick ([everymac](https://everymac.com/systems/apple/ipod/specs/ipod-classic-6th-generation-specs.html)). Stack front→back: front plate + glass; click wheel and LCD on a steel backing plate; metal mid-frame; logic board (full footprint, ~95 × 55 × 1.0 mm, two screws to the frame, connectors on its edges and back face); battery pouch on the board; 1.8" ZIF HDD in foam bumpers — Toshiba MK8022GAA/MK1231GAL/MK1634GAL, 54 × 71 × **5 mm** thin or **8 mm** thick; stainless back shell ([iFixit teardown](https://www.ifixit.com/Teardown/iPod+Classic+Teardown/595)). Usable depth for our board + cell: ~6 mm thin, ~9 mm thick. Board silicon and revisions (A1238 120 GB = Rev A, 2008): S5L8702, K4X51163PE SDRAM, SST25VF080B flash, PCF50635 PMU, CS42L55 codec, LTC4066 charger ([freemyipod](https://freemyipod.org/wiki/Classic)). Not published anywhere: board/mid-frame drawings, 6G connector pitches, battery outline, screw positions.

- **Board outline:** match the original logic board's outline and the mid-frame screw holes (two Phillips screws); design to the mid-frame, not the shell. Original board ~1.6 mm; target 1.0–1.2 mm.
- **Connectors to provide:** click wheel FPC, LCD FPC (stock pinout or new panel), headphone/hold flex, battery (3-pin, thermistor), microSD socket, USB-C mid-mount.
- **Click wheel:** keep; serial protocol documented in Rockbox; new wheels available from Elite Obsolete.
- **Port aperture:** 30-pin opening ~21 × 3 mm; USB-C with printed/CNC filler. Keep the port centred.
- **Antenna keep-out:** near the headphone jack's plastic insert or under the wheel.
- **Thermal/EMI:** metal shell; no thermal issue at RP2350 power levels.
- **Reversibility:** no adhesive on original parts; foam spacers where the HDD was.
- Cosmetic parts if needed: Elite Obsolete (shells, wheels, faceplates), Moonlit Market, iFlash (shells, screws, tools).

**Click wheel sub-board (how it works):** a capacitive ring of ~16 copper segments under the plastic plus five dome switches, scanned by a microcontroller on the flex itself (Cypress PSoC on the 5G; same family on the 6G). The PSoC interpolates finger position to 96 steps per revolution and streams 32-bit frames on its own ~53 kHz clock while touched; the host only listens (one init packet). It has its own 3.0 V LDO fed from the system rail. All the touch logic therefore stays in the wheel — our board needs 4 lines + power and a frame decoder (design doc §4.1).

**Click feedback — direction set 3 Nov 2026:** the final board carries **both** a piezo footprint (stock feel) and an LRA footprint (LD0832AA, stocked); A/B on the testbed before freezing. The Click feedback setting becomes piezo / haptic / off. Background: The stock iPod has no haptic: the click is a **piezo buzzer on the logic board** driven as a tone (Rockbox `piezo-6g.c`). Options: (a) piezo only — authentic, ~£0.50, one GPIO/PWM; (b) LRA or ERM haptic via DRV2605L on I²C — the Moonlit Classic Connect feel, ~£5, needs board area near the wheel; (c) both, selectable in settings. Board footprints for both are cheap; the decision is about mid-frame space and whether the original feel matters.

## Firmware

Click-wheel-driven UI recreating the Classic experience (menus, Cover Flow, podcasts, search), audio pipeline with software codecs, BT/Wi-Fi stacks, USB mass storage. This is the largest cost in the project — more than the hardware.

- Starting points: Rockbox `ipod6g` target (LCD init, click wheel protocol, headphone detect); Sprout's open-source firmware (MPL-2.0) once published at github.com/seanpod/sprout; sPot (Guy Dupont) for a Pi/Spotify UI.
- Wi-Fi use cases: podcast download, library sync from a NAS/PC, firmware OTA. Streaming (Spotify/Apple Music) needs a Linux module.
- Host side: the iPod must mount as a plain USB drive — no iTunes dependency.
- Keep the stock-panel driver and a new-panel driver behind one display abstraction so either screen works.
- **RAM budget:** every feature ADR states its RAM cost (the Pico 2 W's 520 KB is no longer a limit since 12 Nov 2026: the product target is the RP2350B with PSRAM, [ADR-0001 amendment](adr/0001-rp2350-phase1-platform.md#amendment-12-november-2026-the-pico-2-w-is-frozen-the-product-target-is-the-rp2350b-with-psram)). The final board has 8 MB PSRAM (APS6404L, QSPI CS1, [ADR-0013](adr/0013-bluetooth-ram-psram.md)); artwork, HTTP and AAC are PSRAM-only.
- **Flash and OTA:** final board 16 MB W25Q128; bootrom A/B partitions with TBYB ([ADR-0014](adr/0014-ota-ab-partitions.md)).
- **Secrets:** RP2350 random 256-bit key in OTP at provisioning, chip-ID HKDF as the pre-provisioning fallback, no secure element; ESP32 route: eFuse key + flash encryption. Secure boot decided later ([Wi-Fi](wifi.md#secrets-at-rest)).
- **Clock:** [ADR-0015](adr/0015-clock-plan.md).

## Cost

About £35–55 in parts per board (RP2350 route ~£25–40), before the donor iPod and battery; budget £300–600 for two or three prototype spins including assembly. Figures are approximate, low-volume UK pricing, October 2026.

| Item | Estimate |
| --- | --- |
| Compute: RP2350B / ESP32-S3 / T113 | £1–5 |
| Compute: Zero 2 W or CM Zero (module route) | £14–20 |
| DAC + headphone amp (CS43131 / ES9018K2M) | £5–8 |
| Charger, fuel gauge, regulators | £4–6 |
| Wi-Fi/BT (CYW43439 or ESP32-C6, if not on-chip) | £2–5 |
| microSD socket, USB-C, FPC/ZIF connectors | £3–4 |
| Passives, crystals, ESD, antenna | £3–5 |
| PCB, 4-layer, JLCPCB, 5 pcs | £20–40 per batch (6-layer ~double) |
| Assembly | £5–15 per board, or hand-build |
| Battery 2,000–3,000 mAh | £10–15 |
| Donor Classic | £25–40 (already bought) |
| Replacement IPS panel (optional) | £3–6 |
| Thick rear shell (optional) | ~£10 |

## Prior art

| Project / resource | What it is | Link |
| --- | --- | --- |
| Sprout Pico / Pi (certain things, London) | Open-source RP2350 replacement mainboard for iPod 3G/4G/mini; BT, Wi-Fi, CS43131 DAC, USB-C, 2 TB microSD; Pi version adds CM Zero for streaming. CERN-OHL-S v2 / MPL-2.0. Pre-launch on Crowd Supply | [crowdsupply.com](https://www.crowdsupply.com/certain-things/sprout-pi-mainboard), [certainthings.studio](https://certainthings.studio/sprout/) |
| sPot (Guy Dupont, 2021) | 4G gutted, Raspberry Pi Zero W, colour screen, Spotify client, BT; code on GitHub, write-up on Hackaday | [9to5mac](https://9to5mac.com/2021/01/28/developer-modifies-4th-gen-ipod-with-built-in-wi-fi-and-spotify/) |
| Zac Builds iPod | 5G with iFlash, USB-C transplant, BT module, IPS screen | [yankodesign](https://www.yankodesign.com/?p=551626) |
| Moonlit Market Classic Connect 2 | All-in-one BT 5.2/Qi/haptic/USB-C kit for Classics, stock SoC | [ifun.de](https://www.ifun.de/classic-connect-2-bluetooth-qi-und-usb-c-fuer-den-ipod-classic-277730/) |
| Boxy Pixel kit | Aluminium/glass shell + BT/USB-C charge board | [hardware.com.br](https://www.hardware.com.br/noticias/mod-kit-ipod-classic/amp/) |
| iFlash | SD/mSATA storage adapters, compatibility tables | iflash.xyz |
| Rockbox | Open firmware; `ipod6g` target holds the LCD and click-wheel driver code | rockbox.org |
| iFixit guide #572 | Opening and logic-board removal photos | [ifixit.com](https://www.ifixit.com/Guide/iPod+Classic+Logic+Board+Replacement/572) |
| Communities | r/ipod (wiki, build threads); YouTube: DankPods, Zac Builds, Macho Nacho |  |

## Open items

- [ ] Open the donor; photograph both sides of the original logic board.
- [ ] Measure board outline, screw-hole positions, connector positions (wheel, LCD, headphone, battery, HDD ZIF).
- [ ] Measure the LCD window aperture and the LCD bay in the mid-frame (for an IPS panel swap).
- [ ] Measure usable cavity depth for the battery with the HDD removed.
- [ ] Identify the stock LCD and click-wheel FPC pin counts and pitches.
- [ ] Decide: RP2350-only vs RP2350 + Linux-module footprint.
- [ ] Decide radio: RM2 module vs ESP32 (WROOM-32E) vs ESP32-S3 + QCC module ([ADR-0007](adr/0007-bluetooth-audio-path.md); bare CYW43439 ruled out 3 Nov 2026). The ESP32-C6 co-processor is ruled out for A2DP: BLE only, no classic BT.
- [ ] Confirm Compute Module Zero availability and price.
- [ ] Watch for Sprout's GitHub release (hardware files and firmware).

### Added at export (1 Nov 2026)

- [ ] Click wheel — measure the 6G connector and confirm the line mapping; see the
  [design page](design.md#open-items) for the full list.
- [ ] ST7789 (Adafruit 2.0") panel parameters — MADCTL, RGB/BGR, offsets, inversion, backlight
  polarity — to be found on the bench; see the [design page](design.md#open-items).
- [x] Click feedback: both footprints, A/B on the testbed (3 Nov 2026) — see
  [Reused parts and mechanical](#reused-parts-and-mechanical).
- [ ] Recording requirement implies an ADC path; CS43131 dropped, codec in [ADR-0012](adr/0012-audio-codec-final-board.md) —
  stereo check on the ES8311 outstanding.
