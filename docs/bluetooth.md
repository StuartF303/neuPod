# Bluetooth: BTstack, the settings store and the plan

Decision background: [ADR-0007](adr/0007-bluetooth-audio-path.md) (BTstack on the CYW43439 for phase 1),
[ADR-0011](adr/0011-bare-metal-audio-core.md) (core 1 rules). Roles: neuPod is an **A2DP source** (SBC, and SBC-XQ =
SBC at a high bitpool) and an **AVRCP target** (plus a controller side where absolute volume needs it); the headset is the
sink. HFP comes later. This page covers what **stage 1** and **stage 2a** (pairing and connection management) built, and the plan for the rest of stage 2 and for stage 3.

## Staged plan

| Stage | Content | State |
| --- | --- | --- |
| 1 | RAM trim; SD-backed settings store (volume, backlight, click feedback, and BTstack's link keys); BTstack classic running on core 0; HCI bring-up; device discovery with a Settings > Bluetooth screen; host simulation | built |
| 2a | Pairing (SSP "just works") and one saved headset; connection states (Pairing, Connecting, Connected, Disconnected, Failed with a reason); Connect / Disconnect / Forget; Settings > Bluetooth rows and the Devices pairing flow; link keys on the SD card; host simulation and tests. **No audio.** See [Pairing and the connection](#pairing-and-the-connection-stage-2a) | built; bench: Pair / save / reconnect / Forget passed on a Sony WI-C200. Disconnect, re-pair after Forget, wiped-headset refusal and Wi-Fi + Bluetooth coexistence are deferred to 2b |
| 2b | A2DP source with SBC / SBC-XQ; audio routing from core 1 through a new core 1 to core 0 PCM path (storage shared with the I2S ring, [ADR-0013](adr/0013-bluetooth-ram-psram.md) amendment) with the source-side pacing model below; negotiated codec on the Now Playing badge; Audio output Auto / Wired; pause on link loss; status-bar glyph. See [Stage 2b](#stage-2b-the-audio-stream-built). **Not built: hi-res decimation, 48 kHz.** | built, host-tested; bench pending (Y2 first) |
| 2c | AVRCP: the headset's absolute volume is the volume (software at 0 dB), headset buttons, track metadata. See [Stage 2c](#stage-2c-avrcp-volume-buttons-metadata-built) | built, host-tested; bench pending (Y2 first) |
| 3 | Reconnect on power-up and after a drop (**built**, [below](#reconnecting-stage-3-built)); polish (error states, auto-sleep of the radio); the ADR-0007 listening test (SBC-XQ against a QCC3008 board, AirPods Pro 3); power measurements | reconnect built, host-tested; the rest planned |

## The stack

* **BTstack** from the Pico SDK 2.3.1 (`lib/btstack`, git `eb0bb8b5e`, `btstack_version.h` = 1.6.2): CMake targets
  `pico_btstack_classic` (HCI, L2CAP, SDP, AVDTP, A2DP, AVRCP ...), `pico_btstack_hci_transport_cyw43` (HCI over the
  CYW43439's shared bus, with the SDK's chip support and the controller's Bluetooth firmware), `pico_btstack_run_loop_async_context`
  and `pico_btstack_sbc_encoder` (linked now to prove it builds; nothing calls it until stage 2, so it costs no flash).
  Classic only: no BLE, no mesh. Configuration: `platforms/rp2350/btstack/btstack_config.h` (static pools, no `malloc`).
* **Not used**: the SDK's `pico_btstack_cyw43` glue, because it stores BTstack's pairing data in the **internal flash**
  (`pico_btstack_flash_bank` on `flash_safe_execute`). See "Why the SD card" below.
* **Our glue** ([ADR-0019](adr/0019-bluetooth-audio-sink-boundary.md)): the BTstack code is one platform-neutral adapter plus a small
  port per platform.
  * `hal/common/bt_btstack/bt_btstack.c` implements the Bluetooth half of `hal/include/hal_radio.h` (`hal_bt_*`): discovery, the
    pairing and connection state machine, the timeouts, and the link key database on top of the settings store (`hal_bt_storage_t`,
    wrapped as a `btstack_tlv_t`). Nothing BTstack-specific crosses `hal_radio.h`. `hal/host/hal_radio.c` is the host simulator.
  * `hal/common/bt_btstack/bt_port.h` is what a platform provides: `bt_port_present()` (is the controller usable), `bt_port_start()`
    (memory, run loop and `hci_init()` with the platform's transport), `bt_port_run_sync()` and `bt_port_post()` (hand work to
    BTstack's thread, blocking or not), `bt_port_power()`, `bt_port_storage()` (the storage BTstack's thread may use),
    `bt_port_deliver()` and `bt_port_poll()` (events out). The local address comes from the controller on the RP2350; the Y2 port
    reads it before HCI starts. No locking is part of the port: the adapter's state is touched by BTstack's thread only, or through atomics.
  * RP2350: `platforms/rp2350/hal/bt_port_cyw43.c`: the CYW43 transport, the `async_context` run loop, and the SDK's
    `btstack_cyw43_init()` / `btstack_cyw43_deinit()` hooks (`cyw43_arch_init()` calls them), as before the split.
  * Y2: `platforms/y2/hal/bt_port_stpbt.c`: `/dev/stpbt` through `btstack_uart_stpbt.c` (one H4 packet per `read()`, the device is not
    a tty) and `btstack_chipset_mtk_consys.c` (the MediaTek vendor script that must run before HCI_Reset), both shared with the
    `y2-a2dp` bench tool (`testbed/y2/tools/y2-a2dp`), and the POSIX run loop on a thread of its own. Configuration:
    `platforms/y2/btstack/btstack_config.h`; BTstack's source list is `cmake/btstack_posix.cmake` (the Pico SDK's copy, as for the tool).
* **Controller**: the CYW43439 on the Pico 2 W. Its Bluetooth device address is the Wi-Fi MAC plus one (the SDK transport sets
  it). `CYW43_ENABLE_BLUETOOTH=1` is set once for everything that links `neupod_freertos`, because `cyw43_arch_init()` is
  compiled into several static libraries and every copy must agree.

## Threading

BTstack is single-threaded; which thread that is depends on the port. Every `hal_bt_*` call can be made from any RTOS task and only
hands a request over (`bt_port_post()`) or reads an atomic; the BTstack code itself runs on one thread.

**RP2350.** BTstack runs on the **async context of the CYW43 driver**, which with `pico_cyw43_arch_sys_freertos` is one FreeRTOS
task, the "CYW43 worker" (core 0, priority 3, **6 KB** stack; it was 4 KB before BTstack). Every BTstack callback, and our packet
handler, runs on that task.

| Caller | How it reaches BTstack | Blocks the caller? |
| --- | --- | --- |
| radio boot check (`hal_bt_init`) | `async_context_execute_sync` (`bt_port_run_sync`) | yes, until the worker has queued the HCI power-on (milliseconds) |
| UI task (`hal_bt_inquiry_start` / `_stop`; stage 2a: `hal_bt_pair` / `_connect` / `_disconnect` / `_forget`) | `btstack_run_loop_execute_on_main_thread` (`bt_port_post`; a static callback registration each; the state is reserved with an atomic exchange first, so one request is in flight) | no |
| radio worker to the UI | `hal_bt_event_fn` callback (`bt_port_deliver` calls it at once), which `app/runtime/bt_service.c` turns into a 16-deep queue (`os_queue_send`, no wait: an event is dropped rather than holding the radio up) | no |

Core 1 never calls any of this, and BTstack never runs on it. The rules in [Runtime](runtime.md) hold unchanged;
`tools/check_core1_closure.py` is still green on the linked image.

**Y2.** The firmware is a Linux process whose RTOS is the FreeRTOS POSIX port: tasks are pthreads that the port runs one at a time
and its critical sections are global state, so a thread that is not an RTOS task must never call a FreeRTOS function (queue, mutex,
event group, delay). BTstack therefore runs on the **POSIX run loop on a pthread of its own** (512 KB stack, every signal blocked,
including the port's SIGALRM tick, which is sent to the running task's thread only). It never calls the app and never touches the settings
store:

| Direction | How |
| --- | --- |
| task to BTstack | `bt_port_post()`: tasks take turns (an atomic flag, sleeping through the scheduler) and call `btstack_run_loop_execute_on_main_thread`, whose own native lock then only BTstack's thread and one task contend for. `hal_bt_init` waits with `hal_delay_ms` (scheduler sleep), up to 15 s, never a native wait |
| BTstack to the app | events go into a 64-slot lock-free ring; `hal_bt_poll()` (the UI frame calls it through `bt_service_tick`) delivers them from the calling task, in order, one task at a time. The callback therefore runs on that task, not on the radio thread: same rules (quick, no blocking, no calls back into `hal_bt_*`). A ring that overflows drops the new event and says so on the console |
| link keys | `platforms/y2/hal/y2_bt_tlv.c`: BTstack's thread reads and writes a RAM copy of the link-key records (preloaded from the settings store in `hal_bt_init`); the same poll writes changes back to the store, retrying about once a second while the store refuses (no card, read-only, not loaded). Pairing works without a writable card; the key then lives for the session only |

Start-up on the Y2: `hal_system_late_init()` calls `y2_bt_probe()`, which reads `/data/BT_Addr` (read only, 6 bytes, MSB first; an
unreadable, short, all-zero or all-0xFF file means Bluetooth unavailable, never a default address) and opens `/dev/stpbt` on a helper
thread with signals blocked (the open powers the chip and takes about 1.3 s). Bluetooth is available only if both worked. The descriptor is
kept and handed to the transport, so the device is opened once. `hal_bt_init` then sets BTstack up (the MediaTek vendor script, with the
address, runs before HCI_Reset), starts the thread and powers HCI on. There is no Wi-Fi on the Y2 (`hal_wifi_present()` is false), so the
boot check reports Wi-Fi unavailable while Bluetooth is available. Chip bring-up before the firmware starts is `S80mtk-bt`
([Y2 testbed](https://github.com/StuartF303/neuPod/blob/main/testbed/y2/README.md)).

The controller does not report a page timeout at the configured time (the MT6582 only answers after Create_Connection_Cancel), so an
unanswered Pair or Connect is ended by our own timer (`HAL_BT_PAIR_TIMEOUT_MS` / `HAL_BT_CONNECT_TIMEOUT_MS`), which cancels the page and ends the
attempt as "timed out"; a late Connection_Complete with an error is ignored.

## Boot and what the UART shows

The "radio" boot check task (4.8 s deadline, then it keeps waiting up to 12 s so a slow bring-up still upgrades the result) calls
`hal_system_late_init()` (CYW43 driver, which also starts BTstack's memory and run loop through `btstack_cyw43_init`), then
`bt_service_start()`, which sets the local name (`neuPod`), the Class of Device (Audio, portable audio), makes the device
**never discoverable, and not connectable or bondable** (nobody can find it; only while a Pair action runs is it connectable and bondable, for the chosen headset only, stage 2a) and calls `hci_power_control(HCI_POWER_ON)`.

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

Built and host-tested; Pair, save, reconnect and Forget passed on the bench (Sony WI-C200). Disconnect, re-pair after Forget, the refusal of a headset that wiped its key, and Wi-Fi + Bluetooth coexistence are **deferred to the stage 2b bench**. No audio: the connection ends at "the other side has an A2DP sink".

**HAL** (`hal/include/hal_radio.h`, `hal_bt_*`; the hardware half is `hal/common/bt_btstack/bt_btstack.c`, the host simulator `hal/host/hal_radio.c`):

| Call | What it does |
| --- | --- |
| `hal_bt_pair(dev)` | The Pair action. **Never discoverable.** Connectable and bondable **on** (with auto-accept and the "no input, no output" IO capability) for this headset only: a connection filter (`gap_register_classic_connection_filter`) rejects every other address, and a link key that appears for any other address is deleted at once. Outgoing ACL link, SSP "just works" authentication (the link key goes to the SD-card store), then an SDP query for an A2DP sink (UUID 0x110B) on the same link. Everything goes off again when it ends. 30 s limit (`HAL_BT_PAIR_TIMEOUT_MS`) |
| `hal_bt_connect(addr)` | Connect to a device paired before: SDP query first (it creates the link), then authentication with the **stored key** (`gap_request_security_level`). Not connectable, not bondable, and the IO-capability request is refused (IO capability UNKNOWN), so no transient pairing can authenticate the link. A Connect with **no stored key** (checked before it starts, again before authenticating, and again on the result) fails with `HAL_BT_FAIL_AUTH`; so does a headset that lost its key, instead of silently pairing again. 20 s limit |
| `hal_bt_disconnect()` | Cancels a Pair / Connect, or drops the link. Reported as DISCONNECTED, never as FAILED |
| `hal_bt_forget(addr)` | Deletes the link key (drops the link first if it is the current peer). `HAL_EBUSY`: both request slots are busy, retry. `HAL_ENODEV`: the stack is OFF, STARTING or FAILED: delete the record straight from the store with `hal_bt_storage_delete_key()` (what `bt_saved_delete_key()` does; BTstack reads its keys from the store on demand) |
| `hal_bt_link_state()` / `_link_reason()` / `_link_device()` / `hal_bt_accepting()` | State, failure reason, the device the state is about (also after a failure, so a screen can tell whose failure it is), and whether the radio is discoverable or connectable right now (only while Pair runs) |

States (`hal_bt_link_state_t`): `DISCONNECTED`, `PAIRING`, `CONNECTING`, `CONNECTED`, `FAILED` (with `hal_bt_fail_t`: `NOT_FOUND` no answer, `AUTH` pairing refused or stored key rejected, `NO_SINK` no A2DP sink,
`TIMEOUT`, `LINK_LOST` an established link ended by the other side, `INTERNAL` the stack refused or went down, `BUSY` a Pair the headset ended or had no resources for: busy with something else or not in pairing mode, `PIN` a pre-2.1 headset that asks for a PIN). One phrase per reason, `hal_bt_fail_text()`, serves the console and the UI. A link change is a `HAL_BT_EVT_LINK` event (state, reason, device). It is separate from the
radio state (`hal_bt_state_t`), except that a Pair or Connect needs the radio ON and not scanning, and a scan is refused while a link is being set up (`HAL_EBUSY` both ways). One link at a time.
There is **no reconnect** at power-up or after a drop: stage 3.

**The saved headset** (`app/include/bt_saved.h`, `app/runtime/bt_saved.c`): settings key **`NPbd`** (`SETTING_BT_DEVICE`), 10 to 42 bytes: version 1, address, class (24 bit), name. The pairing key itself is BTstack's `BTL` record (tag "BTL" + raw slot number 0..7) in
the same store (sealed). Capacity: the store holds 48 tags; the documented worst case (Wi-Fi networks, 8 link keys, resume, settings) was about 38, now about 41; a host test fills 47 other tags and still stores it, and checks that a full store refuses it
cleanly. Rules, all tested: a device is remembered only after a Pair **ended CONNECTED**; a failed or cancelled Pair leaves the previous headset alone; a second headset **replaces** the first, and the first one's key is deleted
(a queue of four addresses under a mutex, drained by `bt_service_tick`; an address stays queued until the radio takes it; a link event carries its attempt number and `bt_saved` ignores events of an attempt older than its pending Pair); a Pair that got as far as making a key and then failed (no A2DP sink) has its key deleted as well. If the store is **not writable** (card read-only or absent, so the store is RAM-only) a Pair still works for the session but the screen says "Paired - not saved (read-only)" and the console `bt: pair not persisted (store not writable)`; Forget then keeps the record and leaves the address in the stale queue until the card is writable (`bt: record kept ...`; the queue does no work, and does not touch the radio, while the store is unwritable: `bt: key delete waits ...`, then `bt: store writable, draining ...`; a new Pair of the same address removes its queue entry). A failed re-pair of the saved headset drops the record even then, since BTstack already deleted the key in RAM, because deleting only in RAM would bring the headset back at the next boot. `bt_saved_pair_begin` refuses (EBUSY) while another Pair candidate is live. If the record **cannot be saved** (store full) the new key is deleted, the link is dropped and the Devices screen says "Can't save: settings full". Re-pairing the **remembered** headset deletes its old key when the bonding starts (BTstack does that), so if that Pair fails or is cancelled the record is cleared too. Forget deletes the key and the record.
`bt_service_pair` / `_connect` / `_disconnect` / `_forget` (`app/runtime/bt_service.c`) are what the UI calls. Link events are handled in the radio callback, not by a screen, so a Pair that finishes while the user has left the screen is still remembered.

**UI** ([UI](ui.md)): Settings > Bluetooth gains, once a headset is saved, three rows under "Scan for devices": its **name** (state on the right: Not connected / Pairing... / Connecting... / Paired, no audio yet / Failed: reason, with busy dots while working),
**Connect** (becomes **Disconnect**, or **Cancel** while working) and **Forget** (asks for a second Select within 4 s). The right-hand pane shows the radio state, the address, the headset and its state. On the Devices screen, Select on an audio device stops
the scan if needed and starts the Pair action; the row shows busy dots, then "Paired, no audio yet" or Failed: reason; the first row becomes **Cancel pairing** meanwhile. Menu on the Devices screen cancels a Pair in progress (so the radio does not stay connectable for nobody).

**Failure stance.** Fail closed for the radio: connectable and bondable go off on every way out of a Pair (success, failure, timeout, cancel, leaving the screen, the stack going down), and the 30 s timer is the backstop. Never discoverable. A saved device is written only on success.
Fail open for the features: with no card the saved headset and the keys live in RAM for the session (as the store always does).

**Known limit of 2a.** Nothing holds the link open (the A2DP signalling channel arrives in 2b), so the link closes by itself shortly after the SDP query: by BTstack's idle timeout, or by the headset itself (reason 0x13, remote user terminated, seen on the bench after 1 to 2 s with a Sony WI-C200; BTstack's own idle timeout would be later). Both are reported as DISCONNECTED and logged as `bt: link closed <addr> reason 0x.. (idle)`, not as failures; only abnormal endings (supervision timeout and the like) are `LINK_LOST`. No keep-alive is added. The 2a UI worded a CONNECTED link **"Paired, no audio yet"**; stage 2b opens the A2DP stream right after CONNECTED, which keeps the link up, and the row says "Connected - SBC" / "Streaming" ([Stage 2b](#stage-2b-the-audio-stream-built)).

**Pairing method.** Only Secure Simple Pairing (2.1 and later) is supported. With "no input, no output" IO capability the key is unauthenticated (`LEVEL_2`, "just works"), which is all a headset needs. A pre-2.1 headset that asks for a PIN is refused at once (`gap_pin_code_negative`) and the Pair ends FAILED "PIN not supported". **SSP headsets only.**

**Teardown.** If the link drops during bonding, SDP or authentication from the other side (or by timeout), or locally during SDP or authentication, the attempt ends at once as FAILED (a local disconnect is ignored only while bonding: BTstack sends one itself after a failed bonding and the real bonding result follows), so the Pair window closes without waiting for the timer. Cancelling a connection that is still being created retries `hci_create_connection_cancel` every 200 ms (at most 10 times) when the controller cannot take a command yet. Every Pair / Connect has an attempt number: a disconnect request or an SDP
result that belongs to an earlier attempt cannot end the next one (a query still running from a cancelled attempt makes the next one wait for the SDP client). When BTstack reports the stack OFF, a link or attempt in progress becomes FAILED (`INTERNAL`) and everything is reset.

**Pairing order.** Pair uses `gap_dedicated_bonding` and keeps the link for the SDP query: `btstack_config.h` defines `ENABLE_EXPLICIT_DEDICATED_BONDING_DISCONNECT`, so BTstack reports a good bonding at once and does not disconnect (a failed bonding still disconnects). Without that define the result is only reported after a disconnect, and every Pair timed out (found on the bench). `gap_dedicated_bonding` deletes any old key for the address first, which is what a re-pair wants; Connect cannot use it for that very reason. On real hardware the bonding result arrives before the link-key notification: neither the stray-key filter nor the candidate logic depends on the order (the Pair is still in progress when the key arrives, and a headset is only remembered after the SDP check).

**Console lines** (`bt_btstack.c`, `bt_saved.c`): `bt: pair <addr>`, `bt: connect <addr>`, `bt: connected <addr>`, `bt: <addr> failed: <text>`, `bt: link lost 0x..` (abnormal end of an established link), `bt: link closed <addr> reason 0x.. (idle, no profile yet)` (clean idle drop), `bt: link dropped in set-up 0x..`, `bt: bonding 0x..`, `bt: dropped stray key <addr>`, `bt: forgot <addr>`, `bt: pair not persisted (store not writable)`, `bt: key delete waits (store not writable)`, `bt: record kept (store not writable)`.

### Stage 2b to-do from the 2a review

* Bench: Disconnect, re-pair after Forget, a headset that wiped its key (Connect must refuse), Wi-Fi + Bluetooth coexistence.
* A host test that links `hal/common/bt_btstack/bt_btstack.c` against a BTstack stub and injects event sequences (bonding failure 0x16 then 0xD9, remote 0x13 during SDP, timeout in each phase, stack off mid-attempt, Forget of the current peer, PIN request). Today only the simulator in `hal/host/hal_radio.c` is tested.
* A boot-time sweep that drops any BTstack link key whose address is not the saved record (orphan keys after a damaged record or a power cut before the stale queue drains).

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
| Content | tag (32-bit) to value (up to 96 bytes), up to 48 tags (`SETTINGS_MAX_ITEMS`; worst case about 41 with the saved headset): volume (`NPvo`), backlight (`NPbl`), auto-dim and its idle time (`NPad`/`NPat`), output path of the saved volume (`NPvp`), the resume record (`NPr0`/`NPr1`, [Audio](audio.md#resume-last-track-and-position)), shuffle and repeat (`NPsh`/`NPrp`), click mode (`NPck`: 0 off, 1 haptic, 2 piezo, 3 both), haptic actuator (`NPha`) and calibration (`NPhc`), Wi-Fi settings and networks ([Wi-Fi](wifi.md)), BTstack link keys (`BTL` + slot 0..7, 28 bytes plaintext, stored sealed since the secrets work, up to 8), the remembered headset (`NPbd`, 10 to 42 bytes, stage 2a) and later device records. The same tag/value/length model as BTstack's TLV, so `hal_bt_storage_t` (in `hal_radio.h`) maps one to one onto `btstack_tlv_t` |
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
| flash image (`text`) | 1,263,280 B | 1,285,888 B | **+22,608 B** (SDP client and L2CAP now linked and used, the pairing state machine, the saved-headset module, the UI rows) |
| `data` | 0 | 0 | 0 |
| static RAM (`bss`) | 488,896 B | 489,340 B | **+444 B** (L2CAP and SDP client state, the link state machine, the saved-device candidate and the four-deep key-deletion queue) |
| free SRAM (`__HeapLimit - __end__`) | 15,320 B | 14,876 B | **-444 B** |

BTstack heap use: none. `HAVE_MALLOC` is not defined and every pool is a static array in `.bss`; the pool counts in `btstack_config.h` did not change (the 444 B above is everything that moved). The FreeRTOS heap and the task stacks are unchanged;
the new code runs on the 6 KB radio-worker stack, so its high-water mark is on the bench list. No internal-flash write code is linked (`flash_safe_execute`, `pico_flash`, `btstack_tlv_flash_bank` are absent from the ELF). The core-1 closure check reports ok.

BTstack pools (`btstack_config.h`) are sized for **one** headset: 1 HCI connection, 4 L2CAP channels, 1 AVDTP connection, 2 stream endpoints, 2 AVRCP entries (the two roles of one link, since 2c),
8 remembered link keys (the UI remembers one headset; the other seven slots are headroom); ACL buffers 1021 + 4 bytes. Stage 2a needs 1 HCI connection and 1 L2CAP channel (the SDP query), so they were not touched. Revisit them in 2b when the real stream is up.

## Licence

BTstack is **not** free for commercial use. Its own licence is non-commercial only; Raspberry Pi holds a grant that lets purchasers of its
Pico W / Pico 2 W / RM2 products use it in their products. The details, and what is **not** settled, are in the third-party table of
[`LICENSE.md`](https://github.com/StuartF303/neuPod/blob/main/LICENSE.md). In short: fine on the Pico 2 W bench rig; a final board
with a bare CYW43439 chip (or any non-Raspberry-Pi radio route) needs a written answer from BlueKitchen / Raspberry Pi, or a commercial
BTstack licence, before anything is sold or distributed. The cyw43-driver has the same shape of licence (non-commercial, with a Raspberry Pi
grant for use with its chips).

## Stage 2b: the audio stream (built)

**Headroom.** The Bluetooth output is attenuated by a fixed `NEUPOD_BT_HEADROOM_DB` (3 dB, `audio_dsp.h`) before the 16-bit conversion
(TPDF-dithered) and SBC. Found on the Y2 with AirPods Pro (12 Nov 2026): with a track mastered to 0 dBFS at volume 0 dB, the decoded SBC
put 80 to 135 samples per 2 s at the rail in loud passages, heard as crackle; the PCM into the encoder was bit-exact and the SBC stream
matched a reference encode on the PC (22 to 27 dB SNR), so the overshoot is SBC's own reconstruction. At -4 dB it was clean. The headset's
volume (AVRCP, stage 2c) makes up the level. Bench aids on the Y2 only: `NEUPOD_BT_HCI_DUMP=<file>` (HCI trace, PacketLogger format) and
`NEUPOD_BT_PCM_DUMP=<file>` (the encoder's input PCM) in `/etc/neupod-fw.env`.

Built and host-tested; **not yet run on a headset in the firmware** (bench steps at the end). The Y2 is where it is proved first: the BTstack A2DP source it uses is the one verified
on AirPods Pro and a Sony WI-C200 by the `y2-a2dp` tool, now inside the shared adapter.

**The stream.** Once the link is CONNECTED the adapter opens an A2DP source stream by itself (`a2dp_source_establish_stream`) with one SBC endpoint: 44.1 kHz stereo, joint stereo, 16 blocks, 8
subbands, bitpool up to the headset's maximum but at most 53 (328 kbit/s). `NEUPOD_BT_SBC_XQ=1` raises the cap to 76 (455 kbit/s); the badge then reads `SBC-XQ` whenever the bitpool in use is above 53.
Both are constants at the top of `hal/common/bt_btstack/bt_btstack.c`. The stream is reported as `HAL_BT_EVT_STREAM` (OPENING, OPEN, STREAMING, SUSPENDED, CLOSED, FAILED with a reason) with the
negotiated `hal_bt_codec_t`; `hal_bt_stream_start / suspend / close / open` are asynchronous requests to BTstack's thread, and `hal_bt_stats()` gives frames encoded, packets sent, underruns, drift
corrections and the link RSSI where the controller reports it. A headset that offers no usable configuration fails with "no common format"; one that refuses the stream with "audio refused".

**Where the PCM comes from.** PCM never passes through `hal_bt_*`. The audio core (core 1) has a second output: with `audio_set_output(AUDIO_OUT_BT, 44100)` its sink is *virtual* (`hal_audio_set_virtual`:
the block clock still ticks every 256 frames at the sample rate, nothing leaves a DAC) and the block interrupt writes each block, rounded to 16 bit (with TPDF dither when the volume is below 0 dB or the source is
deeper than 16 bit) into a **Bluetooth ring**, `app/audio/bt_pcm.c`. The encoder (BTstack's thread) is the one consumer, through `hal_bt_set_pcm_source()` (`hal/include/hal_bt_pcm.h`), lock-free, no RTOS call
(on the Y2 BTstack's thread is not an RTOS task). Core 1 keeps the ADR-0011 rules; `tools/check_core1_closure.py` is green with the new roots (`vclk_isr`) and `bt_pcm.c` is in the source-rule list.

**One store for both rings (ADR-0013 amendment).** The 32 KB `g_pcm_store` has two layouts that never coexist: wired = the whole store is the decoder ring (4096 frames of int32 stereo); Bluetooth = its first half is the
decoder ring (2048 frames, 46 ms at 44.1 kHz) and its second half is the Bluetooth ring (4096 frames of int16 stereo, 93 ms). The layout changes only with the stream closed (`AUDIO_CMD_OUTPUT`). No new static RAM for the rings.

**Pacing, link first.** The headset's clock is not ours, so nothing is a free-running clock:

* The Bluetooth ring is the only buffer between the decoder and the link. When it has no room for a block the block interrupt **waits** (consumes nothing, advances no position, counts `bt_stall_blocks`): the
  decoder, the elapsed time and the end of the track are paced by the link.
* The encoder is a run-loop timer (8 ms) that owes the headset rate x elapsed time of audio, never more than 40 ms of it (no unbounded backlog after a stall), and while a finished packet waits for "can send now"
  it produces nothing. The owed amount is steered by the ring's fill level against half of it: above half the encoder consumes up to 0.3 % faster, below half slower, one step of 0.05 % per tick; each change of step is a "drift
  correction" in the statistics. When a stream starts the excess above half (the ring filled during the set-up) is sent at once, within the 40 ms bound.
* An empty ring while a track plays is an **underrun** (counted once per dry spell, not per tick); an empty ring while paused is not (`active()` of the source). What was owed during an underrun is dropped: no burst when audio returns.
* The SBC storage bound includes the payload header byte (the `y2-a2dp` review fix): the packet holds at most `min(link MTU - 1, storage - 1)` bytes of frames. 680 B of storage on the RP2350 (a typical headset MTU is 672), 1030 B on the Y2.

**Routing (`app/runtime/audio_route.c`).** The saved setting **Audio output: Auto / Wired** (Settings > Bluetooth; default Auto). Auto: Bluetooth as soon as a headset's stream is open. Once the output is Bluetooth it **stays** Bluetooth until the headset is
let go on purpose (Disconnect or Forget) or the setting is Wired. The change needs a quiet audio core: whatever plays is stopped (its place is kept, as for Stop), the audio stream is closed, the output selected, the stream reopened.
The A2DP stream is started when something plays and suspended after 4 s of silence. **Link lost while playing:** the player is paused at once (position kept), the ring is emptied (honoured by the consumer at its next call), the banner "Headset disconnected"
shows for 3 s, nothing plays from the wired output. Play pressed with no headset on the Bluetooth output pauses again with "Headset not connected". A stream the headset released while the link stays up is reopened up to three times.

**What is not in 2b (deviations from ADR-0019 and the brief).**

* **Sample rate: 44.1 kHz only.** The endpoint offers 44.1 kHz; a track at another rate (48 kHz, 88.2 / 96 kHz) is refused on the Bluetooth output ("Not supported on Bluetooth (44.1 kHz only)", and the queue skips it) because there is no resampler and ADR-0003 forbids a cheap one. 48 kHz would need a stream reconfiguration per track (or a resampler); the **hi-res decimation on core 1 (half-band FIR, 2:1 / 4:1) of ADR-0013 is a follow-up**, not built: its coefficients and state have to be measured against the closure check and the 7.7 KB of free SRAM.
* **Volume** is the software volume as on I2S in 2b (absolute volume, the headset keys and the track information are [2c](#stage-2c-avrcp-volume-buttons-metadata-built)).
* **`hal_bt_set_pcm_source`** is an addition to the HAL: ADR-0019 says PCM does not go through `hal_bt_*`; this call only hands the stack the consumer side of the ring.
* The status-bar glyph for "connected" is the text colour, not white: the bar is light.

**Memory (RP2350, Release, `arm-none-eabi-size` on `neupod.elf`, the phase 1 branch before and after):**

| | before 2b | after 2b | delta |
| --- | --- | --- | --- |
| flash image (`text`) | 1,297,748 B | 1,349,812 B | **+52,064 B** (A2DP / AVDTP / the SBC encoder, routing, UI) |
| static RAM (`bss`) | 489,712 B | 496,536 B | **+6,824 B** (SBC encoder state 1,704, SBC packet storage 680, PCM scratch 512 + 1,024, AVDTP / A2DP / SDP pools and records, 16-bit block buffer, the longer 48-byte names) |
| free SRAM (`__HeapLimit - __end__`) | about 14.5 KB | **7,680 B** | -6.8 KB |

The rings add nothing (shared store). The stack of BTstack's worker (6 KB) now also runs the SBC encoder and the A2DP state machine: **check its minimum free stack on the bench** before anything else grows. Decoder ring in Bluetooth mode is half as deep (46 ms): the bench has to show the 44.1 kHz decoders ride that out (`underruns=0`).

**On the board (stage 2b), in this order.**

1. Y2 first: pair, Connect, play a WAV and a FLAC: sound within a second, no gaps for 10 minutes, `bt: a2dp: SBC 44100 Hz, bitpool 53 ..., 328 kbit/s` on the console, About > BT audio underruns 0.
2. Pause for 10 s: the stream suspends (UART `a2dp suspended`), Play resumes it. Switch the headset off while playing: pause at once, banner, the position unchanged when it is back and Connect is pressed.
3. Audio output Wired with the headset connected: nothing on Bluetooth, the wired path (null sink on the Y2, I2S on the RP2350) as before. Auto again: playback stops and the output moves.
4. RP2350 + WI-C200: the same, plus the 6 KB worker stack minimum, core 1 `underruns` on I2S before and after a Bluetooth session (the ring layout comes back), Wi-Fi joining and leaving while streaming (still provisional).
5. A 48 kHz or 96 kHz file on Bluetooth: refused with the message, the queue moves on, nothing crashes.
6. AirPods Pro 3: Connect, music; try `NEUPOD_BT_SBC_XQ=1` on a build and listen (the ADR-0007 test).

## Stage 2c: AVRCP, volume, buttons, metadata (built)

**Roles.** The headset is the AVRCP *target* of absolute volume (category 2) and we are its *controller* for that; for the buttons and the display we are the *target* (category 1, player) and the headset is the controller. BTstack creates both roles on every AVRCP link, so `MAX_NR_AVRCP_CONNECTIONS` is **2** (640 B each) and the SDP records of both roles are registered (107 and 114 bytes). The headset normally opens the AVRCP channel itself (AirPods do); if it has not 2 s after the A2DP signalling channel we open it. The adapter (`hal/common/bt_btstack/bt_btstack.c`, same code on the RP2350 and the Y2) follows the verified `y2-a2dp` tool.

**HAL (`hal_radio.h`, ADR-0019 section 1).** `HAL_BT_EVT_VOLUME` (0..127), `hal_bt_volume_set()`, `hal_bt_volume_supported()` / `hal_bt_volume_get()`, `HAL_BT_EVT_KEY` (`PLAY`, `PAUSE`, `PLAY_PAUSE`, `NEXT`, `PREV`, `STOP`, `FF`, `RW`; presses only), `hal_bt_track_info()` (title, artist, album, duration, position, play status, track id; returns false when the stack is busy with the previous call, the app asks again at its next tick). "Absolute volume supported" means the headset **has reported a volume** on the AVRCP channel (the answer to our volume-change registration): it turns true a moment after CONNECTED and false when the channel or the link goes; a headset that rejects the registration never turns it true and the software volume stays. Our own volume commands are sent without waiting for each answer (BTstack allows several in flight), the latest value wins, and the headset's reports that cross our commands are ignored so the bar does not jump back. No FreeRTOS call is made on the BTstack thread (the Y2 rule of `bt_port_stpbt.c`): the track information is one copy under a try-lock that neither side waits on, events leave through `bt_port_deliver()`.

**Volume policy (`app/runtime/bt_control.c`).** On the Bluetooth output with absolute volume the wheel moves the headset's volume (`hal_bt_volume_set`, 4 of 127 per step, 32 steps lock to lock), the software volume is held at 0 dB (`player_volume_hold_unity`; the user's software level is kept and comes back when the headset is not the volume), and a swipe on the stem moves the bar. Without absolute volume, or on the wired output, the software volume is the volume as before. One scale (`bt_vol_to_db` / `bt_vol_from_db`): 127 = 0 dB, 0 = the software "off" level (80 dB), linear in dB, so a wheel step is about 2 dB in both modes; host-tested. On connect the headset's volume is read and shown; **nothing is ever pushed to the headset** (no restore of an earlier level). If it reports 0 the bar is empty, the label says `Headset 0%` and the status line says `Playing - headset volume 0%` (the AirPods Pro that a phone left at 0 % are no longer silently "playing"). Deviation: **no per-headset volume is stored** (asked as "if cheap"): the headset keeps its own level, which is the truth, and re-applying an old level behind the user's back is a loudness risk.

**Buttons.** Done by the monitor task, so they work with any screen open or none: Play/Pause toggle; PLAY only when not playing and PAUSE only when playing (a repeated or crossed press does no harm); STOP **pauses** (the place is kept; a later PLAY resumes); NEXT / PREV are Right / Left (a folder queue is needed, and Previous restarts a track more than 3 s in); FF / RW scrub by the Now Playing step (a 60th of the track, 1 to 30 s) per press, so holding the button repeats. A button press does not wake the backlight.

**Track information.** While a link exists the track's title (the file name without a title tag), artist, album, length, position and play status are handed over when the track or the status changes, when the position jumps more than 2 s (a seek), and every 10 s. The stack extrapolates the position while playing, answers the headset's play-status queries, and sends the track-changed and playback-status notifications. Nothing is sent for a test tone.

**UI.** Now Playing: `Headset 62%` and a bar on the same 0..127 scale; Test tone the same. Settings > Bluetooth pane: `Absolute volume: yes / no` (the pane no longer says `On` while a stream is open, to make room). Tours at 320x240 and 480x360: `np-bt-volume`, `np-bt-volume0`, `bt-key-next` ([UI](ui.md)).

**Memory (RP2350, Release):** `text` 1,350,480 B to 1,374,856 B (**+24,376 B**, AVRCP, the volume / key / metadata policy and the reconnect policy, UI), `bss` 496,548 B to 498,596 B (**+2,048 B**: two AVRCP connections 1,280 B, SDP records 232 B, track information and state about 400 B), so free SRAM falls from about 7.7 KB to **about 5.6 KB**. The 6 KB worker stack now also runs the AVRCP code: its minimum is on the bench list. The core-1 closure check reports ok.

**On the board (stage 2c), in this order.**

1. Y2, AirPods Pro, connected after the iPhone left them at 0 %: Now Playing shows `Headset 0%` and the hint; turn the wheel: sound, the bar follows; swipe the stem: the bar follows (console: `bt: avrcp: headset volume N%`).
2. Press the stem / buttons: Play/Pause, double and triple press give Next / Previous. The track changes on the device; the headset display (if any) shows title and artist.
3. A headset without absolute volume (a speaker): `Absolute volume: no`, the wheel is the software volume as before.
4. The WI-C200 and the RP2350: the same, plus the worker stack minimum and the free SRAM.

## Reconnecting (stage 3, built)

`app/runtime/bt_reconnect.c` (pure policy) driven by `bt_service.c`. After the link to the remembered headset is **lost** (`HAL_BT_FAIL_LINK_LOST`: out of range, switched off without saying goodbye) Connect is tried again after 2 s, 5 s, 10 s, then every 30 s, for 5 minutes in all. At power-up, once Bluetooth is on and the settings are loaded, one attempt is made at once and the same schedule follows if it fails. Every attempt is ended by the HAL's own connect timer (20 s; the MT6582 does not report its page timeout until the connection is cancelled). **Any action of the user ends it for good** (Disconnect, Forget, Pair, Connect, Cancel); a running scan or somebody else's Pair / Connect only holds it back. A headset that hangs up politely (idle disconnect, `0x13`) is not "lost" and is not chased. The headset row reads `Reconnecting...` with busy dots from the loss until it is back, or given up. **Playback is not resumed** by a reconnect: the pause from the drop stays (Play, or the headset's button, continues). Not built: auto-resume of playback.

## Stage 2 design notes (2a, 2b and 2c built)

* **Priority and RAM ([ADR-0013](adr/0013-bluetooth-ram-psram.md), amendment of 4 Nov 2026):** stage 2 follows the Audio Pack tone test and MP3/ALAC playback. The original funding (sizing the codec arena, releasing LVGL's second render buffer)
  no longer pays: the arena saving was already spent in #17, and free SRAM is now about 15 KB (15,320 B measured on `main`, 14,876 B with stage 2a). The **core 1 to core 0 Bluetooth PCM ring reuses the 32 KB I2S PCM ring storage**: the outputs are
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
* **AVRCP** (2c, built, [above](#stage-2c-avrcp-volume-buttons-metadata-built)): the headset's absolute volume is the player's volume, play/pause/next/previous from the headset's buttons, and track metadata for the headset display.
* **UI** (2a built: pairing flow, saved device and state in the pane): still to come, negotiated codec and battery (if the headset reports it) in the pane; the Now Playing badge shows
  the codec ([UI](ui.md)).

## What to check on the board (stage 2a)

Only the hardware can confirm these; the host simulator and tests cover the state machine, the saved record and the UI.

1. **Pair.** Headset in pairing mode (AirPods Pro 3: case open, button held until the light flashes white). Settings > Bluetooth > Scan for devices > Select the headset: busy dots, then "Paired, no audio yet" within a few seconds; UART shows `bt: pair`,
   `bt: paired with`, `bt: connected to ... (A2DP sink found)`. Back on Settings > Bluetooth the headset is listed, the right pane shows its name and state.
2. **Never discoverable; connectable only while pairing, and only for the chosen headset.** No phone ever sees "neuPod" in its Bluetooth list, before, during or after a Pair. During a Pair a second device (or the same phone) that tries to connect to neuPod by address is rejected, and no key is created for it; after Connected / Failed / Cancel nothing can connect.
3. **Link key on the card, not in flash.** After pairing, `/.neupod/settings.a` or `.b` has changed and no other path on the card; reboot: the headset is still listed. Nothing writes the internal flash (core 1 and the audio underrun counter unaffected during the Pair; `underruns=0`).
4. **Connect / Disconnect / Forget.** Disconnect, Connect: reaches Connected again with the stored key and **without** the headset being put in pairing mode (it must be on and not connected to something else). Forget (twice Select): the row disappears; Connect is gone; pair again works.
   Key lost on the headset side (forget neuPod in its own settings or factory reset it): Connect shows "Failed: pairing refused", it does not silently re-pair.
5. **Failures.** Headset switched off: Connect shows "Failed: not found" within the 20 s limit. A phone as the target: "Failed: not a headset" and no saved device afterwards. Walk away from the headset until it drops: "Failed: link lost" (or "Not connected" if it closed an idle link).
   Menu on the Devices screen during a Pair cancels it.
6. **The idle link.** (2a) The headset hung up after 1 to 2 s without a profile; since 2b the stream keeps the link up.
   Also: Connect with the key deleted from the card by hand fails at once with "pairing refused" and no radio traffic; and a headset that is busy with another phone during a Pair says "busy or not pairing".
7. **Wi-Fi alongside (provisional policy).** With Wi-Fi joined and a transfer running, Pair, Connect and Disconnect: no HCI reset, no Wi-Fi drop, no stalled transfer. Also with "Wi-Fi while playing" On and a file playing from the card (I2S path, no Bluetooth audio yet): `underruns=0`.
8. **Memory.** Monitor lines `heap: free=... min_free=...`, `lvgl_mem`, and the radio worker's stack minimum after several Pair / Connect / Forget cycles: the worker's free stack must stay clear of zero (the new code and its `printf`s run there).
9. **Repeat Pair 10 times** (Forget in between) and with a headset that connects back to us by itself right after pairing (we are connectable during the Pair for this): no stuck "Pairing...", no orphan key (Forget leaves the card with no `BTL` record (tag "BTL" + raw slot number 0..7) for it).

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
