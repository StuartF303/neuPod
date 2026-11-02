# Wi-Fi: lwIP, the Wi-Fi service, the clock and secrets at rest

Stage W1 of the Wi-Fi work (ADR-0007 keeps the radio behind `hal_radio.h`). Bluetooth is described in [Bluetooth](bluetooth.md); the on-screen
keyboard in [Wheel input widgets](ui-input.md). Not on the board yet: everything here was built and tested on the PC and compiled for the RP2350.

## Stack and memory

* The CYW43 arch is now the SDK's **lwIP + FreeRTOS** variant (`pico_cyw43_arch_lwip_sys_freertos`). BTstack is unchanged and still runs on the same
  async context (the radio worker task): `cyw43_arch_init()` starts the driver, lwIP, then BTstack, in that order. IPv4 only, no sockets, **no TLS**.
* `platforms/rp2350/lwip/lwipopts.h`: static pools only (`MEM_LIBC_MALLOC` off), `MEM_SIZE` 6000, 12 pbufs, TCP window 4 x 1460, 16 TCP segments, 4 TCP and UDP
  control blocks. lwIP callbacks (DHCP, DNS, SNTP, HTTP) run on **lwIP's own tcpip thread** (priority 3, 1024 words = 4 KB stack on the heap), not on the CYW43 worker; the SDK's FreeRTOS arch serialises the two with the async-context lock. Both, and the wifi task, appear in the monitor's per-task stack-free line, which is the number to watch. Country code GB (channels 1 to 13).
* **Cost** (`arm-none-eabi-size`, whole firmware): static RAM 436,480 B (end of the Bluetooth work) to 476,968 B, **+40.5 KB** (after the review fixes: 32 settings tags and a second staging array, +3.4 KB), of which lwIP is about 28.9 KB
  (pbuf pool 18.4 KB, memory heap 6 KB, TCP and the rest), mbed TLS and the secrets code about 2.5 KB (arena 2 KB), larger settings tables about 2 KB, the rest the Wi-Fi service
  and UI. Flash 1,120,532 B to 1,214,268 B, **+94 KB**: lwIP, the HTTP client, mbed TLS (AES-GCM, SHA-256, HKDF), the CYW43 Wi-Fi code, the Wi-Fi service, the UI widgets.
  The heap is not measured on the board yet: new heap users are the **tcpip thread (4 KB stack)**, the "wifi" task (3 KB), and sockets-less lwIP objects; the radio worker is unchanged at 6 KB (`RT_STACK_RADIO`, checked against `CYW43_TASK_STACK_SIZE` at compile time). Stack overflow checking is on (`configCHECK_FOR_STACK_OVERFLOW 2`).
  The monitor prints `heap: free= min_free=` every 10 s; compare with the Bluetooth figures.

## The Wi-Fi service (`app/runtime/wifi_service.c`)

One task, "wifi" (core 0, priority 1, 3 KB stack), owns the station side. It runs every 200 ms and handles requests from the UI (a queue, never blocking the UI).

| Feature | How |
| --- | --- |
| scan | `hal_wifi_scan_*`: SSID, RSSI, channel, security. The driver's scan cannot tell WPA3 from WPA2 (both carry an RSN element), so secured networks show "WPA2/3"; hidden networks do not appear and are added with "Other network..." |
| join | WPA2-PSK, WPA2/WPA3 mixed and WPA3-SAE, open networks. The cyw43 driver supports all three; a join tries WPA2, then mixed, then WPA3-only, moving on when an attempt fails. Wrong password is reported as such and is not retried in a loop |
| DHCP, DNS | lwIP |
| SNTP | lwIP's client, default `pool.ntp.org` (setting `NPns`), resynchronises about hourly; the answer sets the wall clock |
| remembered networks | up to 4. SSID and a "has password" flag in the clear, the password **sealed** (see below). "Forget" in the network's action menu |
| reconnect | after a failure or a lost link: 2, 4, 8 ... up to 120 s between attempts (`wifi_backoff_t`, host tested); a user request or a success resets it; saved networks are tried in turn |
| power | `wifi_policy_decide()`: Wi-Fi is **off while audio plays** unless "Wi-Fi while playing" is On (default Off); the station is switched off after N idle minutes (default 10; setting); a scan, join, transfer or NTP resync counts as a need. The radio is shared with Bluetooth, and audio comes first |
| diagnostics | UART line every 10 s: `wifi: state=up ssid=.. rssi=.. ip=.. ntp=1 saved=n reconnects=n failures=n lwip mem=used/size (max) pbuf=used/size (max) errs=n`; Extras > Network test |

The radio is switched with `hal_wifi_start()` / `hal_wifi_stop()`. **`hal_wifi_stop` means "radio idle"**: the station interface is taken down (netif removed, link left) but the CYW43 chip itself stays powered, because Bluetooth runs on it; it is not a WLAN power-down and saves less current than the name suggests (measure it).

Settings stored: `NPwf` (enabled, during playback, idle minutes, zone), `NPns` (NTP server), `NPtu` (test URL), `NPn0..3` (SSID records), `NPp0..3` (sealed passwords).

### Coexistence with Bluetooth and audio

Wi-Fi and Bluetooth share the CYW43439 and its bus, and lwIP, BTstack and the driver share the radio worker task. Mitigations: Wi-Fi is off during playback by default (the radio is then free for
Bluetooth audio in stage 2); BTstack's controller flow control settings (`btstack_config.h`) limit what is in flight; the worker has a 6 KB stack. Core 1 (audio) never touches any of this. What to measure
on the board: Bluetooth scan while joined, an HTTP transfer while a file plays (`audio:` `underruns=`), and the effect of "Wi-Fi while playing = On".

## The clock (`app/net/sys_clock.c`)

`hal_millis()` stays the monotonic time for audio and timeouts. The wall clock is UTC (from SNTP, or set by hand in Settings > Wi-Fi > Time) as an offset from it.
There is no time zone database: a zone is a base UTC offset plus "follows the EU rule". Presets: UTC, London (GMT/BST, the default), Central Europe (+1), Eastern Europe (+2). The EU rule: summer time
runs from 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of October. Tests check both edges to the second for 2024 to 2027 (`ctest -R wifi_logic`).
The home pane shows `HH:MM  Mon 2 Nov` once the time is known and `--:--` before. There is no battery-backed RTC: after a power cycle the clock is unknown until the next NTP answer.

## Boot check and status icon

The "wifi" boot check is **ok when the radio driver and lwIP are up**, not when a network is joined. A degraded check means the driver itself failed. "No network saved" is
shown in Settings > Wi-Fi (status row and the right-hand pane), not as a boot failure. The status bar shows a Wi-Fi symbol: hidden when the station is off, grey while idle or connecting, green when up, red after a failure.

## Secrets at rest

What is stored: Wi-Fi passwords and Bluetooth link keys (and any later secret). How:

* **AES-256-GCM** from the Pico SDK's **mbed TLS 3.6** (Apache-2.0 option of its Apache-2.0 / GPL-2.0-or-later licence). Chosen over BTstack's `rijndael` because that is AES block encryption only: GCM
  (authenticated encryption) would have meant writing our own mode, which is the wrong thing to hand-roll. Only `aes`, `gcm`, `cipher`, `sha256`, `md`, `hkdf` and support files are compiled, with our own
  configuration (`app/crypto/neupod_mbedtls_config.h`), about 10 KB of flash, and a 2 KB static arena as its allocator.
* **Key**: `key_provider_get()` (`app/crypto/key_provider_chipid.c`) returns a 32-byte key = HKDF-SHA-256 over the RP2350's 64-bit unique chip ID with a fixed application salt. The ID is the OTP chip ID read by the
  boot ROM before `main` (the SDK's `pico_get_unique_board_id`); **it does not read the flash** (on the RP2040 the same call talks to the flash chip, which would stall core 1; RP2350 does not). The host uses a fixed test ID.
  **This is the only place that knows where the key comes from**: moving to OTP or a secure element means replacing that one file.
* **Record**: version byte, 12-byte random nonce (new for every write), ciphertext, 16-byte tag; the record's settings tag is the authenticated data, so a record cannot be copied to another tag. A password of up to 63
  characters fits. Anything that does not authenticate (another neuPod, a card edited on a PC, damage) reads as **absent**: the Wi-Fi screen shows "password needed" and asks again, BTstack pairs again.
* Existing plaintext Bluetooth link keys (from before encryption) are sealed on the first load. FAT does not scrub freed clusters, so the old plaintext may remain on the card in free space; the card is not safe for that old data.
* Plaintext copies (the sealed value's plaintext, typed passwords in the keyboard widget and its result) are wiped after use.
* NTP answers earlier than 2025-01-01 are rejected; the clock re-anchors hourly so the 49.7-day millisecond wrap is harmless.

### What this does NOT protect (read this)

The RP2350 has **no EEPROM and no battery-backed NVRAM**, and this firmware has no secure boot. The chip ID is **not secret**: it can be read with a debugger, and boards often show it as the USB serial number. So the
encryption protects a **lost or copied microSD card on its own**; it does **not** protect against someone who has the card **and** the board (they can derive the same key). SSIDs are stored in the clear on purpose.

Possible later hardening (each needs a separate, explicit decision; some are irreversible and nothing here touches OTP): a random 256-bit key written into a read-locked **RP2350 OTP page** together with
secure boot; or a secure element (ATECC608 or SE050 class) on the final board; an ESP32-S3 variant would use eFuse keys with HMAC and encrypted NVS. All of them replace `key_provider_get()` only.

## Throughput test

Extras > **Network test**: an HTTP GET (no TLS) of the URL in settings (default `http://192.168.1.10:8000/test.bin`, change it with Select on the URL row; serve a file with
`python3 -m http.server 8000` on the PC), body discarded, KB/s reported. It runs on the radio worker while audio plays, so the check is the `audio:` line. **Not built:** the variant that writes the download to
an SD scratch file under `/.neupod/tmp` through the scoped write window (needs a ring between the radio worker and the storage task); today the target is discard only.

## Staged plan

| Stage | Content |
| --- | --- |
| W1 (this) | lwIP, the Wi-Fi service, join/scan/forget with the wheel keyboard, SNTP and the clock, sealed secrets, diagnostics |
| W2 | device HTTP API and a C# companion app for syncing music and podcasts (no TLS on the device: podcasts through the companion) |
| W3 | polish: roaming, AP list caching, captive portal detection, Wi-Fi during playback measurements, SD-file throughput test |
| W4 | over-the-air updates (needs a dual-image layout and signing; core 1 and flash rules apply) |

## What to check on the board

1. Boot: `boot: wifi ok : driver and lwIP up`, `bt:` lines as before, Bluetooth still ok. Splash with both radios solid.
2. Settings > Wi-Fi > Scan: your networks appear with signal; Select your home network, type the password with the wheel (Select types, Menu deletes, the last key `show` unmasks, Play joins). The status
   shows the address and the signal; `wifi: joined ...` on the UART.
3. NTP: the home screen shows the time (and the right zone, BST in summer) a few seconds after joining. Reboot: it is `--:--` until the network is back.
4. Power cycle: it reconnects by itself; the password is not asked again. Move the card to another board (or edit the file): "password needed".
5. Play a file: the Wi-Fi symbol disappears (radio paused) unless "Wi-Fi while playing" is On. With it On, run the Network test and watch `audio:` for `underruns=0`.
6. Settings > Bluetooth > Scan while joined: still finds the headset.
7. Read `heap:`, `lvgl_mem=` and the `wifi:` line's lwIP figures after all that.
