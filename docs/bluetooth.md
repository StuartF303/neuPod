# Bluetooth: BTstack, the settings store and the plan

Decision background: [ADR-0007](adr/0007-bluetooth-audio-path.md) (BTstack on the CYW43439 for phase 1),
[ADR-0011](adr/0011-bare-metal-audio-core.md) (core 1 rules). Roles: neuPod is an **A2DP source** (SBC, and SBC-XQ =
SBC at a high bitpool) and an **AVRCP target** (plus a controller side where absolute volume needs it); the headset is the
sink. HFP comes later. This page covers what **stage 1** and **stage 2a** (pairing and connection management) built, and the plan for the rest of stage 2 and for stage 3.

## Staged plan

| Stage | Content | State |
| --- | --- | --- |
| 1 | RAM trim; SD-backed settings store (volume, backlight, click feedback, and BTstack's link keys); BTstack classic running on core 0; HCI bring-up; device discovery with a Settings > Bluetooth screen; host simulation | built, **not yet run on the board** |
| 2a | Pairing (SSP "just works") and one saved headset; connection states (Pairing, Connecting, Connected, Disconnected, Failed with a reason); Connect / Disconnect / Forget; Settings > Bluetooth rows and the Devices pairing flow; link keys on the SD card; host simulation and tests. **No audio.** See [Pairing and the connection](#pairing-and-the-connection-stage-2a) | built, **not yet run on the board** |
| 2b | A2DP source with SBC / SBC-XQ; audio routing from core 1 through a new core 1 to core 0 PCM path (storage shared with the I2S ring, [ADR-0013](adr/0013-bluetooth-ram-psram.md) amendment) with the source-side pacing model below; hi-res decimation on core 1; negotiated codec on the Now Playing badge | planned |
| 2c | AVRCP target with absolute volume, headset buttons, track metadata | planned |
| 3 | Reconnect on power-up and after a drop; polish (error states, auto-sleep of the radio); the ADR-0007 listening test (SBC-XQ against a QCC3008 board, AirPods Pro 3); power measurements | planned |

## The stack

* **BTstack** from the Pico SDK 2.3.1 (`lib/btstack`, git `eb0bb8b5e`, `btstack_version.h` = 1.6.2): CMake targets
  `pico_btstack_classic` (HCI, L2CAP, SDP, AVDTP, A2DP, AVRCP ...), `pico_btstack_hci_transport_cyw43` (HCI over the
  CYW43439's shared bus, with the SDK's chip support and the controller's Bluetooth firmware), `pico_btstack_run_loop_async_context`
  and `pico_btstack_sbc_encoder` (linked now to prove it builds; nothing calls it until stage 2, so it costs no flash).
  Classic only: no BLE, no mesh. Configuration: `platforms/rp2350/btstack/btstack_config.h` (static pools, no `malloc`).
* **Not used**: the SDK's `pico_btstack_cyw43` glue, because it stores BTstack's pairing data in the **internal flash**
  (`pico_btstack_flash_bank` on `flash_safe_execute`). See "Why the SD card" below.
* **Our glue**: `platforms/rp2350/hal/hal_radio_bt.c` provides `btstack_cyw43_init()` / `btstack_cyw43_deinit()` (the SDK's
  `cyw43_arch_init()` calls them), implements the Bluetooth half of `hal/include/hal_radio.h` (`hal_bt_*`), and wraps
  the settings store as a `btstack_tlv_t` for the link key database. `hal/host/hal_radio.c` is the host simulator.
* **Controller**: the CYW43439 on the Pico 2 W. Its Bluetooth device address is the Wi-Fi MAC plus one (the SDK transport sets
  it). `CYW43_ENABLE_BLUETOOTH=1` is set once for everything that links `neupod_freertos`, because `cyw43_arch_init()` is
  compiled into several static libraries and every copy must agree.

## Threading

BTstack is single-threaded. It runs on the **async context of the CYW43 driver**, which with
`pico_cyw43_arch_sys_freertos` is one FreeRTOS task, the "CYW43 worker" (core 0, priority 3, **6 KB** stack; it was
4 KB before BTstack). Every BTstack callback, and our packet handler, runs on that task.

| Caller | How it reaches BTstack | Blocks the caller? |
| --- | --- | --- |
| radio boot check (`hal_bt_init`) | `async_context_execute_sync` | yes, until the worker has queued the HCI power-on (milliseconds) |
| UI task (`hal_bt_inquiry_start` / `_stop`; stage 2a: `hal_bt_pair` / `_connect` / `_disconnect` / `_forget`) | `btstack_run_loop_execute_on_main_thread` (a static callback registration each; the state is reserved with an atomic exchange first, so one request is in flight) | no |
| radio worker to the UI | `hal_bt_event_fn` callback, which `app/runtime/bt_service.c` turns into a 16-deep queue (`os_queue_send`, no wait: an event is dropped rather than holding the radio up) | no |

Core 1 never calls any of this, and BTstack never runs on it. The rules in [Runtime](runtime.md) hold unchanged;
`tools/check_core1_closure.py` is still green on the linked image.

## Boot and what the UART shows

The "radio" boot check task (4.8 s deadline, then it keeps waiting up to 12 s so a slow bring-up still upgrades the result) calls
`hal_system_late_init()` (CYW43 driver, which also starts BTstack's memory and run loop through `btstack_cyw43_init`), then
`bt_service_start()`, which sets the local name (`neuPod`), the Class of Device (Audio, portable audio), makes the device
**not discoverable, not connectable and not bondable** (nobody can find or pair with it, except while a Pair action runs, stage 2a) and calls `hci_power_control(HCI_POWER_ON)`.

Expected UART lines (about 1 to 3 s after the splash starts; the exact time is a thing to measure on the board):

```
bt: HCI up, local BD_ADDR 28:CD:C1:xx:xx:xx
bt: BTstack 1.6.2; controller HCI version 11 (Bluetooth 5.2) revision 0x.... , LMP 11, manufacturer 0x.... , LMP subversion 0x....
boot: bluetooth ok      : HCI up, BD_ADDR 28:CD:C1:xx:xx:xx
boot: wifi      DEGRADED: no Wi-Fi driver yet (Bluetooth comes first)
boot: chk_radio  task stack free min .... B
settings: no file yet (defaults) (0 items, seq 0)      (or: loaded (n items, seq n))
```

(The version numbers are what the controller reports; the values above are only the expected shape.) If HCI does not come up:
`bt: HCI did not come up` or `bt: hci_power_control(ON) failed (n)` and the check shows "bluetooth stack down".

## Discovery

`hal_bt_inquiry_start(10)` runs a general inquiry for 10 s (8 units of 1.28 s). Each result carries the address, the Class of
Device, the signal strength (RSSI) and the name when it was in the extended inquiry response. The inquiry mode is set to "RSSI and
extended inquiry response" (`hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR)` before power-on); BTstack's default is the plain mode,
which carries neither. If the controller refuses the Inquiry command (BTstack gives no event for that) the scan ends at once with
`bt: inquiry refused by the controller (status 0x..)`.
**Audio sinks** are devices whose Class of Device major class is *Audio/Video* (bits 12..8 = 4: headsets, speakers, car kits).
After the inquiry the radio asks for the names of audio devices that had none, one at a time (at most 6; a name request cannot
overlap an inquiry). Up to 12 devices are kept per scan. Stopping a scan skips the name requests.

Settings > Bluetooth ([UI](ui.md)) shows the status, the address and a **Scan for devices** row. Select opens the **Devices** screen,
which starts the scan as it opens and lists everything found as it arrives: audio devices with a speaker symbol first (after the scan
ends), then others greyed out, with the signal on the right. Its first row is `Stop scanning` / `Scan again`. Selecting an audio device pairs
with it (stage 2a, below). Menu goes back and stops the scan.

A device only answers an inquiry while it is **discoverable**: headphones in pairing mode, or a phone sitting on its Bluetooth
settings page. Headphones that are connected to something else, or a phone with the screen off, do not appear.

Console lines (UART), for testing without seeing the panel:

```
bt: scan start (10 s)
bt: found 5C:xx:xx:xx:xx:xx cod=0x240404 rssi=-52 name='...' audio=yes
bt: named 5C:xx:xx:xx:xx:xx cod=0x240404 rssi=-52 name='AirPods' audio=yes     (name arrived from a name request)
bt: inquiry complete (3 results delivered by BTstack, 3 kept)
bt: scan done, 3 devices
```

"Delivered by BTstack" counts every result before the 12-device limit; if it is 0 the controller heard nothing, if it is more than
"kept" the limit was hit.

## Pairing and the connection (stage 2a)

Built, host-tested, **not yet run on the board**. No audio: the connection ends at "the other side has an A2DP sink".

**HAL** (`hal/include/hal_radio.h`, `hal_bt_*`; the hardware half is `platforms/rp2350/hal/hal_radio_bt.c`, the host simulator `hal/host/hal_radio.c`):

| Call | What it does |
| --- | --- |
| `hal_bt_pair(dev)` | The Pair action. Discoverable + connectable + bondable **on**, outgoing ACL link, SSP "just works" authentication (the link key goes to the SD-card store), then an SDP query for an A2DP sink (UUID 0x110B) on the same link. All three go off again when it ends. 30 s limit (`HAL_BT_PAIR_TIMEOUT_MS`) |
| `hal_bt_connect(addr)` | Connect to a device paired before: SDP query first (it creates the link), then authentication with the **stored key** (`gap_request_security_level`). Not discoverable, not connectable, not bondable: a headset that lost its key fails with `HAL_BT_FAIL_AUTH` instead of silently pairing again. 20 s limit |
| `hal_bt_disconnect()` | Cancels a Pair / Connect, or drops the link. Reported as DISCONNECTED, never as FAILED |
| `hal_bt_forget(addr)` | Deletes the link key (drops the link first if it is the current peer) |
| `hal_bt_link_state()` / `_link_reason()` / `_link_device()` / `hal_bt_accepting()` | State, failure reason, the device the state is about (also after a failure, so a screen can tell whose failure it is), and whether the radio is discoverable or connectable right now (only while Pair runs) |

States (`hal_bt_link_state_t`): `DISCONNECTED`, `PAIRING`, `CONNECTING`, `CONNECTED`, `FAILED` (with `hal_bt_fail_t`: `NOT_FOUND` no answer, `AUTH` pairing refused or stored key rejected, `NO_SINK` no A2DP sink,
`TIMEOUT`, `LINK_LOST` an established link ended by the other side, `INTERNAL` the stack refused). A link change is a `HAL_BT_EVT_LINK` event (state, reason, device). It is separate from the
radio state (`hal_bt_state_t`), except that a Pair or Connect needs the radio ON and not scanning, and a scan is refused while a link is being set up (`HAL_EBUSY` both ways). One link at a time.
There is **no reconnect** at power-up or after a drop: stage 3.

**The saved headset** (`app/include/bt_saved.h`, `app/runtime/bt_saved.c`): settings key **`NPbd`** (`SETTING_BT_DEVICE`), 10 to 42 bytes: version 1, address, class (24 bit), name. The pairing key itself is BTstack's `BTL?` record in
the same store (sealed). Capacity: the store holds 48 tags; the documented worst case (Wi-Fi networks, 8 link keys, resume, settings) was about 38, now about 39; a host test fills 47 other tags and still stores it, and checks that a full store refuses it
cleanly. Rules, all tested: a device is remembered only after a Pair **ended CONNECTED**; a failed or cancelled Pair leaves the previous headset alone; a second headset **replaces** the first, and the first one's key is deleted
(`bt_service_tick` calls `hal_bt_forget` for it); a Pair that got as far as making a key and then failed (no A2DP sink) has its key deleted as well. Forget deletes the key and the record.
`bt_service_pair` / `_connect` / `_disconnect` / `_forget` (`app/runtime/bt_service.c`) are what the UI calls. Link events are handled in the radio callback, not by a screen, so a Pair that finishes while the user has left the screen is still remembered.

**UI** ([UI](ui.md)): Settings > Bluetooth gains, once a headset is saved, three rows under "Scan for devices": its **name** (state on the right: Not connected / Pairing... / Connecting... / Connected / Failed: reason, with busy dots while working),
**Connect** (becomes **Disconnect**, or **Cancel** while working) and **Forget** (asks for a second Select within 4 s). The right-hand pane shows the radio state, the address, the headset and its state. On the Devices screen, Select on an audio device stops
the scan if needed and starts the Pair action; the row shows busy dots, then Connected or Failed: reason; the first row becomes **Cancel pairing** meanwhile. Menu on the Devices screen cancels a Pair in progress (so the radio does not stay discoverable for nobody).

**Failure stance.** Fail closed for the radio: discoverable, connectable and bondable go off on every way out of a Pair (success, failure, timeout, cancel, leaving the screen), and the 30 s timer is the backstop. A saved device is written only on success.
Fail open for the features: with no card the saved headset and the keys live in RAM for the session (as the store always does).

**Known limit of 2a.** Nothing holds the link open (the A2DP signalling channel arrives in 2b), so BTstack closes an idle link a few seconds after the SDP query: "Connected" turns into "Not connected" by itself. It is reported as
DISCONNECTED, not as a failure. A stage 2b connection keeps it up.

**Pairing order.** Pair uses `gap_dedicated_bonding` and keeps the link (`hci_dedicated_bonding_defer_disconnect`) for the SDP query; that call deletes any old key for the address first, which is what a re-pair wants. Connect cannot use it for that very reason.

**Console lines:** `bt: pair 28:CD:..`, `bt: paired with ..`, `bt: connected to .. 'name' (A2DP sink found)`, `bt: link to .. failed: <reason>`, `bt: connect ..`, `bt: forgot ..`, `bt: link lost (reason 0x..)`.

### Wi-Fi alongside Bluetooth (provisional)

Stuart's decision for stage 2: **allow both**; Wi-Fi follows its own "Wi-Fi while playing" setting and no interlock is built. **This is provisional**: the CYW43439 shares one radio and one bus between Wi-Fi and Bluetooth,
and the decision may be revisited after bench testing. The policy lives in one place, `NEUPOD_BT_WIFI_COEX_ALLOW` in `app/include/runtime_config.h` (default 1); a future interlock hangs off it at `bt_service_pair()` / `bt_service_connect()` and in `wifi_service`.
With 1 nothing is checked anywhere. Bench check (stage 2a): Wi-Fi joined and an NTP / HTTP transfer running, then Pair, Connect and Disconnect: no HCI reset, no dropped link, no Wi-Fi drop, and the 6 KB radio-worker stack minimum
(`chk_radio` / the worker line) stays above its stage 1 value. Stage 2b repeats it with an A2DP stream.

## Why the SD card holds the pairing data (and never the internal flash)

BTstack normally keeps link keys and its device database in a TLV store in the last sectors of the **internal QSPI flash**.
Erasing or programming that flash at run time means the XIP flash cannot be read for the duration (milliseconds to
tens of milliseconds). **Core 1 executes its code from that same flash** (only the hot path is in RAM) and is deliberately not
`multicore_lockout`-aware (ADR-0011: no RTOS, no locks, no cross-core stop requests), so a flash write from core 0 would stall or
crash the audio core. Therefore **nothing in this firmware writes the internal flash at run time**, and the image contains none of
`pico_flash`, `flash_safe_execute` or `btstack_tlv_flash_bank` code paths in use (checked on the linked ELF).

Link keys and the device database are stored in the **settings store** instead, a small file on the microSD card:

| | |
| --- | --- |
| API | `app/include/settings_store.h`, `app/settings/settings_store.c`; RTOS glue `app/runtime/settings_service.c` |
| Content | tag (32-bit) to value (up to 96 bytes), up to 48 tags (`SETTINGS_MAX_ITEMS`; worst case about 39 with the saved headset): volume (`NPvo`), backlight (`NPbl`), output path of the saved volume (`NPvp`), the resume record (`NPr0`/`NPr1`, [Audio](audio.md#resume-last-track-and-position)), shuffle and repeat (`NPsh`/`NPrp`), click mode (`NPck`: 0 off, 1 haptic, 2 piezo, 3 both), haptic actuator (`NPha`) and calibration (`NPhc`), Wi-Fi settings and networks ([Wi-Fi](wifi.md)), BTstack link keys (`BTL?`, 27 bytes plaintext, stored sealed since the secrets work, up to 8), the remembered headset (`NPbd`, 10 to 42 bytes, stage 2a) and later device records. The same tag/value/length model as BTstack's TLV, so `hal_bt_storage_t` (in `hal_radio.h`) maps one to one onto `btstack_tlv_t` |
| File | two alternating files `/.neupod/settings.a` and `settings.b`: magic `NPST`, version (2; version-1 files, same layout and at most 32 items, are still read), count, sequence number, items, CRC-32. Little endian. About 4.9 KB at most (48 tags x 101 bytes). Firmware older than the version-2 store does not read these files: it sets them aside as "newer" and starts from defaults (it never overwrites them), so a rollback loses the settings but not the file |
| Reading | `settings_load()` on the storage task right after the card mounted. A file that fails magic, version or CRC gives **defaults** (reported on the UART, replaced by the next save). A newer or older layout is treated the same way |
| Crash-safe save | **no rename and no delete.** The image is written in place over the OLDER (or invalid) of the two files and synced; the other file is never touched. Load takes the valid file with the higher sequence (wrap-safe), so a power cut at any point leaves at least the previous complete state; a torn file just fails its CRC. A file with a NEWER layout version is moved aside as `settings.old` at the next save instead of being wiped (firmware rollback). Host tests cut the power during the write, again during a rewrite after a cut, and on the very first save |
| Who writes | only the **storage task** (`settings_service_poll`: it wakes every 500 ms when idle and looks at the store at most every 250 ms). The first unsaved change is written about 1.5 s later, so a burst of wheel clicks is one write; a failed save is retried after 5 s. **No save while audio is playing** (an SD card can stall for far longer than the audio ring lasts): changes wait for pause, stop, the end of the track or an idle moment, then are written. The cost is that a volume change made during a long track is lost if the power fails before the next pause. While paused the ring is full and nothing consumes it, so a write cannot hurt. Core 1 never touches it |
| No card / degraded | the store is **RAM-only**: every setter and the BTstack storage still work for the session, nothing is written, the UART says so. After 3 failed saves in a row the store stops trying (RAM-only) until restart. Failure stance: fail **safe for the card** (no write is attempted when the card or its mount is not healthy) and fail **open for the features** (settings and pairing keep working in RAM) |
| Reads before the load | BTstack writes are refused until the store knows whether there is a card and has loaded it (the load would otherwise throw them away). With no card at all they are accepted for the session |

### How the write lock is opened, and only for this file

The card is write-locked by default (`hal_storage_set_write_enabled(false)`). `settings_flush()` is the only production code that opens it, and it
does so in one fixed pattern:

1. `nfs_write_scope_enter("/.neupod")`: from now until `nfs_write_scope_leave()` every mutating `nfs_*` call (open for write, mkdir, unlink,
   rename, with both paths) whose normalised path is not `/.neupod` itself or below it **fails with `NFS_ERR_IO`**, on the card and on the host.
   `..` is resolved before the check and the comparison is by path component (`/.neupod2` is outside).
2. `hal_storage_set_write_enabled(true)` (remembering the previous state).
3. save (the five steps above).
4. `hal_storage_set_write_enabled(previous)`, then the scope is left. There is one exit path, so a failed or "power lost" save restores both.

What this does **not** cover, stated plainly: FatFs updates the FAT and the directory entries that belong to files inside `/.neupod`,
and those sectors are shared with the rest of the volume; the scope limits which *files* can be changed, not which sectors the file
system touches while changing them. It also does not stop a raw `hal_storage_write()`, which nothing else in the firmware
calls outside the storage test menus. The store never formats, never creates or deletes anything outside `/.neupod`, and its
paths are compile-time constants. The scope is not recursive and is meant for one caller on one task, so it has no lock.

### Volume restore policy

The saved volume is restored at boot after the load, **but never louder than the power-on default** (-30 dB, `player_restore_volume`, headphone safety,
see [Audio](audio.md)), and only if the user has not changed the volume yet in this session. A saved quiet level (for example -40 dB) comes back; a saved
loud level (for example -10 dB) comes back as -30 dB. The constant is `PLAYER_RESTORE_MIN_ATT_DB`.

**Replacement (design response, 3 Nov 2026; built in `player_volume_restore_att()`):** honour the saved level, except cap at -30 dB when

* the output path differs from the one the level was saved for (Bluetooth ↔ wired ↔ line/optical), or
* a headphone insertion is detected at boot and the saved path was line-out.

The output path is stored alongside the level in the settings store (`NPvp`, an `output_path_t`: wired, line, optical, Bluetooth). Conservative readings
taken where the text is silent: a level saved without a path (older firmware) or a restore when the current path is unknown is capped; line-out and optical
count as different paths (any change of path is a change); a level already quieter than -30 dB is never touched. **Nothing in the firmware reports the output
path yet** (no Bluetooth audio routing, no jack detect), so until a source calls `player_set_output_path()` / `player_set_headphones_at_boot()` every restore
is capped at -30 dB, exactly as before; the policy, the stored path and the tests are in place.

## Memory budget (RP2350, main SRAM 512 KB)

The static RAM ("bss", every figure from `arm-none-eabi-size` on the linked `neupod.elf`):

| | before (feat/codecs) | after stage 1 |
| --- | --- | --- |
| FreeRTOS heap (`NEUPOD_RTOS_HEAP_BYTES`) | 112 KB | **80 KB** |
| LVGL pool (`NEUPOD_LVGL_POOL_BYTES`) | 96 KB | **72 KB** (the host tour still passes) |
| codec arena | 116 KB | 116 KB |
| settings store (items, parse buffers) | 0 | about 5.3 KB at the end of stage 1; 12.7 KB with Wi-Fi ([Wi-Fi](wifi.md)) and 32 items; **about 9.9 KB now with 48 items** (items 5.0 KB + one 4.9 KB image buffer: the load validates the two files in the buffer and parses the winner straight into the items, so there are no staging copies) |
| BTstack (HCI buffers, pools) and the Bluetooth HAL | 0 | about 6.6 KB |
| **bss total** | **481,588 B** | **436,480 B** (45,108 B freed) |
| flash image (`text`) | 1,060,732 B | 1,120,532 B (+59,800 B: BTstack classic, transport, settings, UI) |

The two constants are in `app/os/memory_budget.h`. Heap consumers that grew: the CYW43 worker stack (4 to 6 KB), a transient 4 KB boot-check stack
for the radio check, the Bluetooth event queue (about 1 KB) and the settings mutex. The monitor prints the heap low-water mark and the LVGL pool peak every 10 s, which is
the number to check on the board (earlier bench figures: heap minimum free about 58 KB of 112 KB, LVGL peak about 38 KB, both before this change).

**Stage 2a delta** (clean Release builds of `main` at 4944981 and of the branch, `arm-none-eabi-size` on `neupod.elf`):

| | main | stage 2a | delta |
| --- | --- | --- | --- |
| flash image (`text`) | 1,263,280 B | 1,284,580 B | **+21,300 B** (SDP client and L2CAP now linked and used, the pairing state machine, the saved-headset module, the UI rows) |
| `data` | 0 | 0 | 0 |
| static RAM (`bss`) | 488,896 B | 489,256 B | **+360 B** (L2CAP and SDP client state, the link state machine, the saved-device candidate) |
| free SRAM (`__HeapLimit - __end__`) | 15,320 B | 14,960 B | **-360 B** |

BTstack heap use: none. `HAVE_MALLOC` is not defined and every pool is a static array in `.bss`; the pool counts in `btstack_config.h` did not change (the 360 B above is everything that moved). The FreeRTOS heap and the task stacks are unchanged;
the new code runs on the 6 KB radio-worker stack, so its high-water mark is on the bench list. No internal-flash write code is linked (`flash_safe_execute`, `pico_flash`, `btstack_tlv_flash_bank` are absent from the ELF). The core-1 closure check reports ok.

BTstack pools (`btstack_config.h`) are sized for **one** headset: 1 HCI connection, 4 L2CAP channels, 1 AVDTP connection, 2 stream endpoints, 1 AVRCP connection,
8 remembered link keys (the UI remembers one headset; the other seven slots are headroom); ACL buffers 1021 + 4 bytes. Stage 2a needs 1 HCI connection and 1 L2CAP channel (the SDP query), so they were not touched. Revisit them in 2b when the real stream is up.

## Licence

BTstack is **not** free for commercial use. Its own licence is non-commercial only; Raspberry Pi holds a grant that lets purchasers of its
Pico W / Pico 2 W / RM2 products use it in their products. The details, and what is **not** settled, are in the third-party table of
[`LICENSE.md`](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md). In short: fine on the Pico 2 W bench rig; a final board
with a bare CYW43439 chip (or any non-Raspberry-Pi radio route) needs a written answer from BlueKitchen / Raspberry Pi, or a commercial
BTstack licence, before anything is sold or distributed. The cyw43-driver has the same shape of licence (non-commercial, with a Raspberry Pi
grant for use with its chips).

## Stage 2 design notes (2a built; 2b and 2c not built)

* **Priority and RAM ([ADR-0013](adr/0013-bluetooth-ram-psram.md), amendment of 4 Nov 2026):** stage 2 follows the Audio Pack tone test and MP3/ALAC playback. The original funding (sizing the codec arena, releasing LVGL's second render buffer)
  no longer pays: the arena saving was already spent in #17, and free SRAM is now about 15 KB (15,320 B measured on `main`, 14,960 B with stage 2a). The **core 1 to core 0 Bluetooth PCM ring reuses the 32 KB I2S PCM ring storage**: the outputs are
  exclusive (I2S or Bluetooth, never both), so the same bytes serve whichever sink is active. That adds **no new static RAM**; the SBC encoder state and working buffers are measured in 2b and come out of the free SRAM.
* **Hi-res audio to Bluetooth:** decimated **on core 1** by a 2:1 or 4:1 half-band FIR to 44.1 / 48 kHz, with 24 to 16 bit TPDF dither, before the samples enter the core 1 to core 0 ring. SBC cannot carry more than 48 kHz / 16 bit.
* **Wi-Fi during A2DP:** allowed (Stuart's decision, **provisional**, see [Wi-Fi alongside Bluetooth](#wi-fi-alongside-bluetooth-provisional): one policy macro, no interlock). The stage 2b bench test has to show an A2DP stream stable while Wi-Fi comes and goes.
* **Final board radio:** not a bare CYW43439 (assembly and licence, [ADR-0007](adr/0007-bluetooth-audio-path.md)); the stage 2 code stays behind `hal_bt_*`.

* **Verify while Wi-Fi toggles:** the Wi-Fi service switches the station on and off (playback pause, idle timeout) on the same radio worker that runs BTstack. Before relying on it, check on the board that an A2DP stream stays stable (no dropouts, no HCI resets) when Wi-Fi comes and goes, and with "Wi-Fi while playing" On.

* **Pairing and saved device (2a, built):** see [above](#pairing-and-the-connection-stage-2a).
* **A2DP source**: BTstack's `a2dp_source` with one SBC stream endpoint; SBC at 44.1 kHz, joint stereo, 16 blocks, 8 subbands, bitpool 53 (about 328 kbit/s) as the
  default and a high bitpool (SBC-XQ, up to what the headset's capabilities allow; the AirPods Pro 3 also offer AAC, which we do not do) when the sink accepts it.
  The encoder is `pico_btstack_sbc_encoder` (Bluedroid, Apache-2.0).
* **Audio routing**: today core 1's decoder fills a PCM ring that its own I2S/null sink drains. For Bluetooth, a new **core 1 to core 0 PCM ring** (single producer, single consumer, the
  same `spsc_ring` rules, 16-bit stereo, roughly 150 to 250 ms) receives the decoded and volume-scaled samples; a core 0 "bt audio" task (or the BTstack worker itself) takes
  them, encodes SBC frames and hands media packets to BTstack. Core 1 gains a "virtual sink" mode (the null sink's hardware alarm timing) so the decoder keeps its block cadence; nothing on
  core 1 uses the RTOS, and the ring and its counters join the closure check.
* **Source-side pacing**: a source must deliver samples at the sample rate, but the headset's clock is not ours. The ring is the buffer: the link's own flow control (the controller's ACL buffers, `can send now` events) slows the
  encoder task when the headset consumes slowly, the ring then fills, and the decoder on core 1 (which only decodes when the ring has room) slows with it, so the
  decoder is paced by the link. A fill-level target (about half the ring) with an occasional one-frame drop or repeat on drift keeps it steady; underruns on the Bluetooth path are
  counted like the I2S ones. To be proven on the bench with the AirPods Pro 3.
* **AVRCP**: target with absolute volume (volume changes on the headset move the software volume and the other way round), play/pause/next/previous from the headset's buttons, and track metadata for the headset display.
* **UI** (2a built: pairing flow, saved device and state in the pane): still to come, negotiated codec and battery (if the headset reports it) in the pane; the Now Playing badge shows
  the codec ([UI](ui.md)).

## What to check on the board (stage 2a)

Only the hardware can confirm these; the host simulator and tests cover the state machine, the saved record and the UI.

1. **Pair.** Headset in pairing mode (AirPods Pro 3: case open, button held until the light flashes white). Settings > Bluetooth > Scan for devices > Select the headset: busy dots, then "Connected" within a few seconds; UART shows `bt: pair`,
   `bt: paired with`, `bt: connected to ... (A2DP sink found)`. Back on Settings > Bluetooth the headset is listed, the right pane shows its name and state.
2. **Discoverable only while pairing.** During the Pair, another phone sees "neuPod" in its Bluetooth list; within a second of Connected / Failed / Cancel it no longer does (and cannot connect). At rest, no phone ever sees it.
3. **Link key on the card, not in flash.** After pairing, `/.neupod/settings.a` or `.b` has changed and no other path on the card; reboot: the headset is still listed. Nothing writes the internal flash (core 1 and the audio underrun counter unaffected during the Pair; `underruns=0`).
4. **Connect / Disconnect / Forget.** Disconnect, Connect: reaches Connected again with the stored key and **without** the headset being put in pairing mode (it must be on and not connected to something else). Forget (twice Select): the row disappears; Connect is gone; pair again works.
   Key lost on the headset side (forget neuPod in its own settings or factory reset it): Connect shows "Failed: pairing refused", it does not silently re-pair.
5. **Failures.** Headset switched off: Connect shows "Failed: not found" within the 20 s limit. A phone as the target: "Failed: not a headset" and no saved device afterwards. Walk away from the headset until it drops: "Failed: link lost" (or "Not connected" if it closed an idle link).
   Menu on the Devices screen during a Pair cancels it.
6. **The idle link.** Note how long "Connected" lasts without audio (expected: BTstack closes the idle link after about 10 s; reported as Not connected, not as a failure). Stage 2b keeps it up.
7. **Wi-Fi alongside (provisional policy).** With Wi-Fi joined and a transfer running, Pair, Connect and Disconnect: no HCI reset, no Wi-Fi drop, no stalled transfer. Also with "Wi-Fi while playing" On and a file playing from the card (I2S path, no Bluetooth audio yet): `underruns=0`.
8. **Memory.** Monitor lines `heap: free=... min_free=...`, `lvgl_mem`, and the radio worker's stack minimum after several Pair / Connect / Forget cycles: the worker's free stack must stay clear of zero (the new code and its `printf`s run there).
9. **Repeat Pair 10 times** (Forget in between) and with a headset that connects back to us by itself right after pairing (we are connectable during the Pair for this): no stuck "Pairing...", no orphan key (Forget leaves the card with no `BTL?` record for it).

## What to check on the board (stage 1)

1. Flash `neupod.elf` with the Pico 2 W, open the UART. Watch for the three `bt:` lines above after the splash, `boot: bluetooth ok` and `boot: wifi DEGRADED`.
   The splash should show Bluetooth solid and Wi-Fi with a cross (that is intended in stage 1).
2. Audio must be unaffected: play a file while the radio comes up; the `audio:` line must keep `underruns=0` and the `core1_stack_free=` value must not drop.
3. Settings > Bluetooth: Status "On", the same address as the UART, **Scan for devices** (opens Devices and scans at once) with a headset in pairing mode (AirPods Pro 3: case open, button held until the light flashes white): it
   appears with a speaker symbol, a name after a few seconds and a signal value; other devices around you are greyed out. Select on it starts pairing (stage 2a; see its checklist above). Menu leaves and stops the scan.
4. Change the backlight and Click feedback, switch the board off and on: both should come back. Change the volume to a quiet level and check it returns; set it loud and
   check it comes back at -30 dB. A `/.neupod/settings.a` (and later `.b`) should be on the card; no other file or folder on the card may have changed.
5. Boot once without a card: the UART shows `settings: no usable card ...` and the screens still work.
6. Read the monitor's `heap: free=... min_free=...` and `lvgl_mem=.../...` lines (and `chk_radio` / the worker's stack figure) after using every menu and one scan, and keep the minimums.
