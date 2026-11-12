# ADR-0019: Bluetooth audio output behind a stack-neutral boundary

## Status

**Accepted** (owner decisions, 12 Nov 2026): keep BTstack, and build the audio sink so the stack can be swapped cheaply. The
owner's answers to the design questions are recorded at the end.

## Context

* Stage 2a ([Bluetooth](../bluetooth.md)) built pairing and the connection on the RP2350 + CYW43439 with BTstack. It is behind
  `hal_bt_*` in `hal/include/hal_radio.h`, and the BTstack code sits in one file, `platforms/rp2350/hal/hal_radio_bt.c`. That
  file mixes three things: BTstack's link logic (pairing state machine, SDP, TLV glue), the CYW43 transport and its run loop, and
  the Pico SDK hooks.
* There is no audio yet. `hal_radio_a2dp_connect()` / `_disconnect()` are declared but not implemented.
* On 12 Nov 2026 the Y2 testbed streamed A2DP to AirPods Pro and a Sony WI-C200. It used BTstack over the MT6582's `/dev/stpbt`
  (`testbed/y2/tools/y2-a2dp`, a standalone tool) and played a tone and a FLAC from the card. Two platforms now have a working
  BTstack controller, each with a different transport and run loop.
* **Licence.** BTstack is free for non-commercial use only ([Bluetooth → Licence](../bluetooth.md#licence)). The owner does not
  expect neuPod ever to be sold (12 Nov 2026), so BTstack is acceptable. It still has to stay replaceable: a licensed BTstack, Zephyr's
  host or ESP-IDF Bluedroid ([ADR-0007](0007-bluetooth-audio-path.md) update) are the alternatives.

## Decision

### 1. One boundary: `hal_bt_*`, extended for audio

Everything above the HAL (`app/runtime/bt_service.c`, the player, the audio engine, the UI) sees `hal_radio.h` only. No BTstack
type, header or constant crosses it. That rule already holds; the decision is to keep it for audio. Stage 2b adds, replacing the
`hal_radio_a2dp_*` stubs:

| Addition | Purpose |
| --- | --- |
| `hal_bt_stream_open()` / `_start()` / `_suspend()` / `_close()` | A2DP stream on the CONNECTED link. Asynchronous; progress through events. |
| `hal_bt_codec_t` (`codec`, `rate`, `channels`, `mode`, `bitpool_min/max`, `kbps`) | The negotiated configuration, for the badge and the stats. The enum names SBC, SBC-XQ (and AAC / aptX for a stack or module that has them). |
| `HAL_BT_EVT_STREAM` (`OPENING`, `OPEN`, `STREAMING`, `SUSPENDED`, `CLOSED`, `FAILED` + reason) | Stream state. |
| `HAL_BT_EVT_VOLUME` (0..127, absolute) | AVRCP absolute volume from the headset. |
| `hal_bt_volume_set(0..127)` | Our volume to the headset. |
| `HAL_BT_EVT_KEY` (`PLAY`, `PAUSE`, `PLAY_PAUSE`, `NEXT`, `PREV`, `STOP`) | AVRCP passthrough from the headset buttons. |
| `hal_bt_track_info()` | Title, artist, album, duration and position for AVRCP metadata (2c). |
| `hal_bt_stats_t` | Encoded frames, packets sent, ring underruns, drift corrections, link quality where the controller reports it. |

**PCM does not go through `hal_bt_*`.** It goes through the core 1 to core 0 ring of [ADR-0013](0013-bluetooth-ram-psram.md)
(4 Nov amendment). The encoder side drains the ring. The audio engine treats Bluetooth as one more output: it selects "Bluetooth",
fills the ring, and never knows which stack encodes it.

### 2. BTstack in one adapter, transports per platform

The BTstack code is split into:

* **`hal/common/bt_btstack/`** (platform-neutral, built by every platform that uses BTstack): the link state machine moved out of
  `hal_radio_bt.c`, the A2DP source, the AVRCP target and controller, the SBC encoder feed and pacing (the source-side pacing model in
  [Bluetooth](../bluetooth.md#stage-2-design-notes-2a-built-2b-and-2c-not-built)), the TLV bridge onto `hal_bt_storage_t`, and the
  implementation of every `hal_bt_*` function.
* **Per-platform glue** (`bt_port.h`: init the transport, give the run loop, post to it, local address, power):
  * `platforms/rp2350/hal/bt_port_cyw43.c`: the CYW43 transport and `async_context` run loop, as now.
  * `platforms/y2/hal/bt_port_stpbt.c`: `btstack_uart_stpbt` + `btstack_chipset_mtk_consys` (promoted from the testbed tool) and the
    POSIX run loop on its own pthread.

Swapping the stack means writing a new `hal/common/bt_<stack>/` against the same `hal_radio.h` and the same `bt_port.h`. Nothing above
the HAL changes, and nor do its tests.

### 3. The host simulator implements the whole API

`hal/host/hal_radio.c` grows the stream, codec, volume and key events (and a simulated drop) so every UI and player behaviour below
runs in host tests and tours without a radio. The simulator and the BTstack adapter are two implementations of one contract. A
contract test (`tests/test_hal_bt_contract.c`) drives both where it can: the simulator in CI, BTstack against a fake controller (as
`test_hci_preinit` does in the y2-a2dp tool).

### 4. The Y2 becomes a Bluetooth platform

The firmware on the Y2 gets real Bluetooth through `bt_port_stpbt.c`, instead of the "not present" stubs in
`platforms/y2/hal/hal_radio.c`. The WMT bring-up (stock `wmt_loader` once per boot, `6620_launcher`, the property area, the device
nodes) moves into the Y2 init scripts ahead of the firmware, and `hal_bt_init()` opens `/dev/stpbt`. The testbed rules hold: MTK
blobs are staged from the backup, never committed; NVRAM is never written. `y2-a2dp` stays as the bench tool.

### 5. The UI is part of stage 2b, not an afterthought

Every state that the boundary can report has a screen, and every one is in a host tour with a snapshot:

| Where | What |
| --- | --- |
| **Status bar** | Bluetooth icon: grey = remembered headset not connected, white = connected, accent colour = streaming. Hidden when Bluetooth is unavailable. |
| **Now Playing** | The format badge shows the negotiated codec on Bluetooth output (`SBC 44.1`, `SBC-XQ`); the source format stays in the stats. On Bluetooth the volume bar is the headset volume. |
| **Volume** | With absolute volume the wheel sends `hal_bt_volume_set()` and the software volume stays at 0 dB; the headset's own volume changes move the bar. Without it, the wheel is the software volume, as on I2S. One mapping table, host-tested. |
| **Headset buttons** | `HAL_BT_EVT_KEY` becomes the same player actions as Play / Right / Left, on every screen. |
| **Link lost while playing** | The player pauses at once (ring emptied, position kept). A banner reads `Headset disconnected` for 3 s. The output stays Bluetooth: nothing plays from the wired output by itself. |
| **Settings > Bluetooth** | `Paired, no audio yet` is gone. The headset row shows `Connected - SBC-XQ` / `Streaming` / `Not connected` / `Failed: <reason>`. A new **Audio output: Auto / Wired** row (Auto = Bluetooth whenever a headset is connected; saved on the card). The right-hand pane adds codec, bitpool, kbit/s, underruns. |
| **Devices** | Unchanged apart from the wording above. Pairing a second headset while one is remembered asks to replace it (one headset, as now). |
| **Settings > About** | Bluetooth stats line (frames, underruns, drift corrections). |
| **Errors** | Every new `hal_bt_fail_t` reason gets its phrase in `hal_bt_fail_text()` (19 characters max) and a tour snapshot. |

Tours: `--tour-bt` is extended with `bt-streaming`, `np-bt-badge`, `np-bt-volume`, `bt-dropped` and `bt-key-next`, at 320x240 and
480x360 ([ADR-0018](0018-ui-resolution-profiles.md)).

## Consequences

* `hal_radio_bt.c` is split before the A2DP work starts. That is a no-behaviour-change refactor of stage 2a, proven by the existing
  2a tests and the bench Pair / reconnect / Forget on the WI-C200 (RP2350) before anything new is added.
* The Y2 can prove the A2DP source, pacing, AVRCP and the whole UI on real headsets while the RP2350 path is still RAM-constrained.
  Bugs found there are in shared code, so the fix lands on both.
* A different stack later costs one adapter and its glue. A QCC/BM83 module would be an adapter that speaks the module's UART
  protocol instead of HCI, behind the same API.
* The core 1 rules ([ADR-0011](0011-bare-metal-audio-core.md)) are unchanged: core 1 only fills the ring; all Bluetooth work is on
  core 0. The closure check covers the ring.
* **RAM budget:** the boundary itself costs nothing (header types, one event struct grows by about 24 B). RP2350: SBC encoder
  state and media buffers are measured in 2b and come out of free SRAM (about 14.9 KB). The PCM ring reuses the I2S ring storage
  (ADR-0013 amendment). Y2: not constrained. Flash, RP2350: A2DP + AVRCP + SBC encoder, measured in 2b.

## Owner decisions (12 Nov 2026)

1. **Split now.** The BTstack link logic moves out of `platforms/rp2350/hal/hal_radio_bt.c` into `hal/common/bt_btstack/` before 2b,
   as a no-behaviour-change refactor.
2. **Link lost while playing:** pause and stay on Bluetooth (position kept, banner); no fallback to wired output.
3. **Output selection:** automatic, with an **Audio output: Auto / Wired** row to force wired.
4. **Volume:** AVRCP absolute volume to the headset with the software volume at 0 dB; software volume when the headset has no
   absolute volume.
5. **Y2 as a Bluetooth platform:** yes, the real stack goes into the Y2 firmware (taken as the default; not asked separately).

## Amendment 12 Nov 2026: stage 2b as built

Details and the RAM / flash figures are in [Bluetooth](../bluetooth.md#stage-2b-the-audio-stream-built). Differences from the decision above, all deliberate:

* **`hal_bt_set_pcm_source()`** hands the stack the consumer side of the PCM ring (a small type in `hal/include/hal_bt_pcm.h`). PCM still does not travel through `hal_bt_*`; the stack pulls it from the ring on its own thread.
* **44.1 kHz only.** One SBC endpoint at 44.1 kHz; tracks at other rates are refused on the Bluetooth output. 48 kHz and the hi-res decimation of ADR-0013 are a follow-up.
* **Declared in 2c** (see below): `HAL_BT_EVT_VOLUME`, `hal_bt_volume_set`, `HAL_BT_EVT_KEY`, `hal_bt_track_info`; the Now Playing volume bar and the headset buttons.
* Two new failure reasons, `HAL_BT_FAIL_STREAM` ("audio refused") and `HAL_BT_FAIL_CODEC` ("no common format"); the status-bar glyph uses the text colour for "connected" because the bar is light.

## Amendment 12 Nov 2026: stage 2c as built, and reconnect

Details in [Bluetooth](../bluetooth.md#stage-2c-avrcp-volume-buttons-metadata-built). Differences from the decision above:

* The volume path is the **AVRCP controller** role (the headset is the target of absolute volume); the AVRCP **target** carries the buttons and the metadata. `hal_bt_volume_supported()` / `hal_bt_volume_get()` are added to the boundary, `HAL_BT_EVT_KEY` also has `FF` and `RW`, and `hal_bt_track_info()` returns false when the stack is busy.
* STOP from the headset pauses (the place is kept). No volume is remembered per headset.
* Stage 3 reconnect is built early on the owner's request: after a lost link and at power-up, with backoff, ended by any user action; playback is not resumed.
