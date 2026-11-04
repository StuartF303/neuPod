# Runtime: RTOS, audio core and boot

The `neupod` executable (RP2350 and host) is this runtime. Decisions: [ADR-0011](adr/0011-bare-metal-audio-core.md)
(single-core FreeRTOS on core 0, bare-metal audio core on core 1; amends
[ADR-0009](adr/0009-rtos-freertos-smp.md)) and [ADR-0010](adr/0010-ui-lvgl.md) (LVGL). The RTOS task
table lives in `app/include/runtime_config.h`; this page explains it.

## Cores

| Core | Runs | Rules |
| --- | --- | --- |
| 0 | FreeRTOS (single-core kernel): UI, input, storage, monitor, CYW43 worker, boot checks | normal RTOS code |
| 1 | the **bare-metal audio core**: a main loop (commands + "decoder": silence, test signals, WAV conversion) and the sink's block interrupt (the I2S DMA interrupt); no kernel | the core-1 rules below |

### Core-1 rules

Core 1 code (`app/audio/audio_core.c`, `audio_shared.c`, `audio_dsp.c`, `spsc_ring.c`, the headers they use, and
`platforms/rp2350/hal/hal_audio.c` / `hal_audio_null.c`) must not use: FreeRTOS or `os.h`; stdio (`printf`); `malloc`;
any SDK primitive the FreeRTOS interop reroutes to the kernel (mutex, semaphore, queue,
critical section, `sleep_*`, `busy_wait_*`); `hal_delay_ms`. It talks to core 0 only through
lock-free rings and a seqlock. Hardware registers, the 1 MHz timer reads (`time_us_64`, `hal_micros`)
and the SDK's spin-lock-only alarm API are fine. The ctest `audio_core_rules`
(`cmake/check_core1_rules.cmake`) fails the build on forbidden tokens in those files.

### Hot path in RAM

`on_block`, the ring functions, the alarm interrupt (`block_isr`, driven straight from the timer registers,
no SDK alarm code and no spin lock shared with core 0) and `hal_micros` run from RAM (`NEUPOD_RAM_FUNC`,
`__not_in_flash_func`) so the audio core does not contend with the UI for the XIP flash cache. `memcpy` and
`memset` (newlib, flash) are still used by the ring. Core 1's stack is painted at start and its high-water mark is
printed in the audio line (`core1_stack_free=`).

### Core 0 / core 1 communication

| From to | Mechanism |
| --- | --- |
| core 0 to 1: commands | ring of `audio_cmd_t` (open, close, statistics window, volume, tone, stream start, stream next, pause, stop); several core-0 tasks send, so `audio_ctl.c` serialises them with a mutex |
| core 0 to 1: stream data | byte ring, 32 KB, fed by the storage task while a file plays: raw WAV bytes, compressed FLAC / MP3 bytes, or length-prefixed ALAC packets |
| core 0 to 1: stream slots | two slots (`seq & 1`) with the stream description and its byte length (published once its last byte is in the ring), plus `g_audio_next_valid` (the announced successor, cleared to cancel); see [Audio](audio.md#gapless-playback-and-the-play-queue) |
| core 1 to 0: seek request | request counter plus byte offset, acknowledged by core 0, flush acknowledged by core 1 (`stream_io.h`) |
| core 1 to 0: handshake | counters written only by core 1: streams set up / ended with nothing following (core 0 must not write data for a new stream before the first moves), the sequence number of the open decoder, and gapless handoffs completed (written by the block interrupt) |
| core 1 to 0: events | two rings (one per producer: the block interrupt, the main loop): stream opened/closed, underrun (at most ~1/s), end of track, command error |
| core 1 to 0: statistics | seqlock-published `audio_stats_t`, written only by the block interrupt |
| inside core 1: decoder to sink | PCM ring, 4096 stereo int32 frames (16 blocks, 93 ms at 44.1 kHz) |

Every ring has exactly one producer and one consumer (`spsc_ring.h`: acquire/release loads and
stores only, no read-modify-write, so it is safe across the two cores).

### How audio timing works

With `NEUPOD_AUDIO_SINK=null` (or on the host) the sink arms a 1 MHz hardware alarm from core 1; its interrupt fires every 256 frames
(5.8 ms at 44.1 kHz) on absolute deadlines (no drift) and runs `on_block`, which reads one block from the
PCM ring (silence and an underrun if the decoder starved it), applies the volume ramp, hands it to the sink and
updates the statistics and the amp mute line. The interrupt preempts the decoder (`-DNEUPOD_DECODER_LOAD_PCT=25`
makes it busy-spend 25 % of a block period per block, as the old stub did, to prove it), so block timing does not
depend on decoder time. The main loop sleeps with
`WFI` when idle. Nothing on core 0 can delay it: no shared lock, no kernel, no shared interrupt.
The I2S sink (default on the RP2350, see [Audio](audio.md)) uses the same structure with the DMA block-complete interrupt (`DMA_IRQ_1`, core 1) instead of the alarm.

## RTOS tasks (core 0)

Priority 7 is the highest; the kernel idle task is 0.

| Task | Priority | Stack | Period / trigger | Budget |
| --- | --- | --- | --- | --- |
| kernel timer service | 6 | 1.5 KB | kernel timers | microseconds |
| storage | 5 | 8 KB | request queue; while a file streams, every 1 ms; otherwise every 500 ms | directory listings and volume space for the UI (yields every 32 entries); streams the file from the card in 2 KB reads into the audio data ring; decodes the album art of the playing track in slices (2 ms of work, 1 ms sleep, audio top-ups between, aborts on any request; [UI](ui.md#now-playing-art)); loads the settings store after the card mounts and saves it about 1.5 s after a change ([Bluetooth](bluetooth.md)); later: library scan, track prefetch |
| input | 4 | 2 KB | every 2 ms | well under 1 % (the wheel FIFO overflows after ~4.8 ms) |
| I2C users, while on the bus | 4 (lifted) | n/a | boot checks, the 5 s gauge read | a few ms (see findings) |
| CYW43 worker (SDK) | 3 | 6 KB | radio events; **every BTstack callback and lwIP callback** | LED; Bluetooth (BTstack's one thread); lwIP (via the SDK's FreeRTOS arch; lwIP also has its own 4 KB tcpip thread) |
| wifi (policy) | 1 | 4 KB | every 200 ms | requests from the UI, join/retry with backoff, power policy, SNTP, the 1 s snapshot for the UI ([Wi-Fi](wifi.md)) |
| haptic (click feedback) | 4 | 2 KB | click queue (2 deep, non-blocking for the UI) | a few ms per click; back to standby after 2 s idle |
| UI (LVGL) | 2 | 12 KB | 5 to 30 ms, from LVGL's own timer | frame strip render + 24-line blocking DMA |
| monitor | 1 | 3 KB | 100 ms tick: heartbeat 1 s, gauge 5 s, stats 10 s | negligible |
| boot checks (transient, 6) | 1 | 3 to 5 KB | once at boot | bounded by per-check deadlines |

Core 1 has its own 16 KB stack array and a 116 KB codec arena (both static RAM). Heap: `heap_4`, 80 KB (`NEUPOD_RTOS_HEAP_BYTES`, `app/os/memory_budget.h`), holds task stacks and kernel objects; static RAM beyond that is the LVGL
pool (72 KB, `NEUPOD_LVGL_POOL_BYTES`), the BTstack pools (about 7 KB), the settings store (about 5 KB), the two render buffers (30 KB), the album-art thumbnail (8 KB, `app/runtime/art_service.c`), the directory-listing buffer (about 19 KB) and the audio core's rings and block buffer (about 28 KB), the play queue (3.6 KB) and two track contexts for the stream controller (2.8 KB).
The monitor prints the heap low-water mark and each task's lowest free stack every 10 s. Input is above
the UI so wheel frames are never lost while LVGL renders; the monitor is lowest; boot-check tasks sit
below the UI so the splash keeps animating while they run.

### Inter-task data (core 0)

| From to | Mechanism |
| --- | --- |
| input to UI | queue of `input_event_t` (16 deep; dropped if the UI lags) |
| monitor / battery check to UI | latest value in a mutex-guarded store (`battery_store`) |
| boot checks to UI | `capabilities_t` (mutex-guarded; first result per subsystem wins) |
| I2C bus | one mutex (`rt_i2c_mutex`) around every battery / haptic transaction |
| UI to haptic task | 2-deep byte queue, sent without waiting (`rt_haptic_click`) |
| UI to player | `player.h`: a mutex-guarded state (what plays, volume) plus commands to the audio core; the storage task reports file start and errors; end of file and stream faults are read back from the audio core's counters and statistics |
| radio worker to UI | `hal_bt_event_fn` callback turned into a 16-deep queue of `hal_bt_event_t` by `bt_service.c` (state, device found/named, scan done); non-blocking send, dropped if the UI lags |
| anyone to settings store | `settings_store.h`: RAM image behind a mutex; only the storage task reads or writes the card file |
| UI to storage task | request queue (list a directory, free space, settings load); results come back on a result queue and the UI polls it. The entries live in one static buffer that the UI reads only after the result for its latest request arrived. Why a task: an SD directory scan is hundreds of sector reads and the UI task must never stall |

### UI performance

The monitor prints every 10 s `ui: frames=… frame_us avg/max=… lvgl_mem=used/total (max …, frag …%)`: passes of
`lv_timer_handler` that rendered something (render plus the blocking strip flush), their average and worst time in
the window, and LVGL pool use. Each list screen costs about 15 KB of the pool (its rows are a fixed pool of
objects, whatever the item count), so the pool is 72 KB (it was 96 KB until the BTstack stack needed the room; the bench peak was about 38 KB) and the stack is limited to 8 screens. The host tour also
prints `ui: screen '<name>' lvgl_mem=…` at every snapshot.

### What the monitor prints about audio

Every 10 s:

```
audio sink: i2s rate=44099.488 Hz (requested 44100 Hz, clkdiv=22+37/256) dma_underruns=0 muted=1 source=none audible=0 volume=-30dB pcm_frames=0
audio: blocks=1861 underruns=0 sink_missed=0 period_us min/avg/max=5431/5804/6181 (nominal 5804) jitter_max_us=377 ring_min=1792/4096 decoded=1868 data_bytes=0 events_dropped=0
cpu%(of one core)/stack-free-min: ... (core 0 tasks only)
```

`blocks` rises by about 1723 per 10 s (172 per second); the period and jitter figures are for the last
10 s window. Events (`audio event: stream opened (44100)`, `UNDERRUN`) are printed as they arrive.
A log-only stall check prints `AUDIO CORE STALLED` if the block counter stops for a second; it is silent
on a good run. The figures above are from the **host**, where the clock is a timer thread on a PC; the
RP2350 numbers are measured on the board.
`audio: stats unavailable` / `monitor: audio stats unavailable` means the seqlock read gave up (core 1 stopped
mid-update); the reader is bounded so the monitor keeps running.

## Bench findings (history)

These led to ADR-0011; the details are kept because they are the reason for the architecture.

1. **Event-list corruption.** In the first design (SMP FreeRTOS, audio task woken by `xSemaphoreGiveFromISR`
   from an alarm interrupt on core 1) the board hung within a second: core 1 spun in `vListInsert` holding
   the kernel task lock, core 0 spun on it in PendSV, and the semaphore's wait list showed the audio
   task's event-list item inserted twice.
2. **Lost cross-core yield requests.** With a polled audio wait, core 1 froze about 1.5 s in, in
   `prvCheckForRunStateChange`, waiting for a yield doorbell that never arrived. A watchdog that re-sent the
   doorbell recovered it every time ("AUDIO STALLED ... kicking core 1", "RECOVERED after 1 kick"), which proves the
   doorbell requests were being lost; with that workaround audio ran with 2 s outliers and 89 % decoder load.
   The kernel is the Raspberry Pi fork's SMP RP2350 port (its newest commit); upstream FreeRTOS-Kernel has no
   RP2350 port and its core cannot be combined with the fork's port.
3. **I2C timeouts in the haptic check.** `hal_i2c` timeouts are wall-clock with a 2 ms floor and a
   transaction takes about 0.5 ms; a check task at priority 1 is pre-empted by the UI (priority 2) for a
   whole frame, so the transaction "timed out" on a healthy bus. The I2C users now run at priority 4
   (`RT_PRIO_I2C`) while on the bus; the haptic check still leaves the DRV2605L in standby.
4. **CPU percentages.** FreeRTOS attributes run time only when a task is switched out, so a task that
   runs uninterrupted is under-reported until it next switches; with the single-core kernel the idle
   figure is now meaningful. The percentages are shares of core 0. UI load of 10 to 20 % during the
   first 10 s is the splash (eight pulsing icons and the shimmer re-render two 24-line strips per frame
   with a blocking DMA wait); it falls once the static home screen is up.

## Boot sequence

1. `main()`: `hal_system_init()` (clocks, UART; unchanged order), then `neupod_runtime_run()`
   launches the audio core (core 1), opens the stream, creates the RTOS tasks and starts the scheduler.
2. The UI task initialises the display first, draws the **splash** (wordmark, firmware version and
   git hash, one icon per subsystem) and only then switches the backlight on.
3. It starts the boot checks, each in its own task with a deadline. **All checks are read-only.**
4. Each icon pulses while its check runs, becomes solid when it passes, and gets a **red cross only
   if the subsystem is not fully correct**. The reason for each failure is printed on the UART
   (`boot: storage DEGRADED: card init: NO_CARD`).
5. The splash holds until every check is resolved, at least 1.5 s (`RT_SPLASH_MIN_MS`) and at most 5 s for
   the checks themselves. **If every check passed it fades to the home screen at once. If any icon has a
   cross, the splash is held for another 8 s (`RT_SPLASH_HOLD_MS`) from the moment the checks are done**,
   with a thin bar along the bottom draining over the hold, and one failure reason at a time under the
   icons (`[2/5] storage: card init: NO_CARD`, 1.6 s each; all of them do not fit at 320x240). **Any input
   event skips the hold**: a wheel or button event, or any key on the UART console of the testbed
   (`skip_on_console_key`, set by the RP2350 `main`). Then it fades to the placeholder **home** screen (status bar
   with the battery percentage, list of unavailable features, last input event).

| Check | Passes when | Currently on the bench |
| --- | --- | --- |
| display | panel initialised | passes |
| storage | card identified and the FAT/exFAT volume mounted; writes stay locked (the settings save opens them for `/.neupod` only, see [Bluetooth](bluetooth.md)) | passes with a card |
| battery | MAX17048 ACKs at 0x36, VERSION 0x001X, cell voltage 2.5 to 4.6 V | passes with a cell |
| haptics | DRV2605L ACKs at 0x5A, device ID 7; left in **standby** | passes when fitted |
| click wheel | receiver starts, init packet sent, a valid frame arrives | **degraded** (not wired, init packet unimplemented) |
| audio | the sink's stream runs: the audio core published its rate and served blocks (cannot detect the DAC: I2S is one-way) | passes with the I2S sink; **degraded** with the null sink |
| Bluetooth | the radio driver is up and BTstack's HCI reaches the working state (local address read) | passes on the Pico 2 W (stage 1; not yet seen on the board) |
| Wi-Fi | radio driver and lwIP are up (not: joined a network) | passes on the Pico 2 W (W1; not yet seen on the board) |

Deadlines: storage 4 s, battery and haptics 1.5 s, wheel 1.5 s, audio 0.5 s, radio 4.8 s (the radio check keeps waiting up to 12 s, so a slow Bluetooth start still upgrades its result). A check that
misses its deadline is marked degraded and its later result is ignored (except that radio upgrade). I2C runs at 400 kHz.

### Capabilities

`capabilities_t` (`app/include/capabilities.h`) holds a state (pending, ok, degraded) and a text per
subsystem. The UI and later features read it (`caps_available(CAP_STORAGE)`); the input task, for
instance, forwards wheel events only if the wheel check passed, so a floating unwired receiver
cannot inject presses.

## Input sources and the testbed key map

All input reaches the UI as `input_event_t` through one queue, filled only by the input task (the sole
caller of `hal_input_poll()` and of the console reader). Wheel-sourced events are forwarded only once the
wheel passed its boot check (or when the wheel is a simulation, as on the host); other sources never depend
on the wheel.

| Source | Keys |
| --- | --- |
| UART console on the testbed (`console_keys`) | `w` / `k` scroll up (counter-clockwise), `s` / `j` scroll down (+/-4 wheel positions = one list step); Enter or `e` = Select; `a` = Left; `d` = Right; `p` or Space = Play/Pause; `m` or Backspace = Menu |
| Host simulator window | Up / Down arrows and the mouse wheel scroll; Enter = Select; Left / Right; Space or `p` = Play/Pause; Esc, Backspace or `m` = Menu |

Any input event also skips the splash hold. The UI screens and their keys are described in [UI](ui.md). `hal_input_reinit()` failing (the init packet is still
`TODO(hw)`) is not fatal: the wheel check passes if valid frames arrive within about 1.1 s, otherwise it is
degraded ("not connected").

## Failure handling

* FreeRTOS stack overflow, malloc failure and `configASSERT` (also LVGL assertions) call
  `neupod_panic()`: the message including the task name goes out on the UART through a lock-free
  path, then the board halts, repeating the message and flashing the LED (2 = stack overflow,
  3 = out of memory, 4 = assertion, 5 = other). On the Pico 2 W the only LED is behind the CYW43 chip,
  whose driver needs the running scheduler, so the LED code shows only for task-context
  failures; define `NEUPOD_PANIC_LED_GPIO=<pin>` for a board with a direct LED. A panic resets the audio
  core (RP2350) so nothing keeps playing, and in task context freezes every other task (UI included) except
  the idle task, the timer task and the radio worker, which the LED needs; tradeoff: the radio worker keeps
  running. From an interrupt or inside the kernel nothing blocking is possible, so there it is the UART message
  and a halt only.
* Failing to create a permanent task or runtime object (UI, input, storage, monitor, queues, the I2C mutex) is fatal
  (`neupod_panic`, LED code 3); failing to create a boot-check task only degrades that capability.

## Running it

* RP2350: `scripts/build.sh rp2350`, flash `neupod.elf`; the UART shows the boot lines, a
  `neuPod alive` heartbeat every second and the 10 s statistics.
* Host: `scripts/build.sh host`, then `build/host/hal/host/neupod` (SDL window if available;
  arrows/mouse wheel scroll, Enter/Space/Esc buttons) or
  `neupod --run-ms 8000 --snapshots DIR` (headless, writes `splash.ppm` and `home.ppm`;
  `--splash-hold-ms N` shortens the failure hold, the smoke test uses 1500).
  `ctest --test-dir build/host` runs it as `runtime_smoke`, writing `build/host/snapshots/`.

## Flashing

`scripts/flash.sh` programs, verifies, resets and then explicitly resumes both cores; see
`scripts/README.md`. (OpenOCD's reset otherwise leaves core 1 halted, so the audio core would not run.)

## What is stubbed

AAC decoder (FLAC, MP3, ALAC and WAV play), prefetch (logs requests), home screen (placeholder), click-wheel receiver (pins
unconfirmed), Bluetooth pairing and A2DP (stage 2, [Bluetooth](bluetooth.md)). Not yet measured on hardware with this architecture: block jitter under UI load,
the panic LED, stack margins.
