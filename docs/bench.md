# Bench practice and test order

How the Pico 2 W testbed is driven from the PC, and the agreed order of the next bench tests.

## Tools and sequence

1. `scripts/build.sh rp2350` (add `--clean` after a CMake option change).
2. `scripts/flash.sh [rp2350] [target]` over the Debug Probe (CMSIS-DAP): programs, verifies, `reset halt`,
   then resumes core 1 and core 0. A plain OpenOCD `program ... reset exit` leaves core 1 halted in the
   bootrom (PC `0xDA`) and the audio core never runs; see [`scripts/README.md`](https://github.com/StuartF303/neuPod/blob/main/scripts/README.md).
3. `scripts/console.sh` — UART0 at 115200 on the Debug Probe.

## Driving the UI without the wheel

The wheel is not fitted (the host → wheel init packet is still unknown, see
[Click-wheel protocol](click-wheel-protocol.md#open-questions)). UART keys: `w`/`s` scroll, `e` select, `m` back,
`a`/`d` left/right, `p` play/pause, `l` toggles Hold (the rig has no Power key, [ADR-0020](adr/0020-hold.md)). A burst of keys is
queued (64 events); if the UI ever could not keep up the 10 s statistics print `input: WARNING n events dropped`.

- Console lines end in `\r`; strip it before matching a line in a script.
- Confirm the logged label of the highlighted row before sending Select (`e`); never press Select blind on a list row.

## Practices

- Never `pkill -f <pattern>` from a script: the pattern matches the script's own shell. Kill by PID.
- Headphones **off your head** for the first sound from any new audio path; start with the Pack's gain switch low.
- When a breakout is silent, read the pin state back over SWD before metering (OpenOCD `mdw` on SIO_IN `0xd0000004` and
  IO_BANK0 `GPIOn_STATUS` `0x40028000 + 8n`, no halt needed): it separates firmware from wiring in one step.
- Count Pico pins from pin 1 (GP0, by the USB connector); GND pins 3/8/13/18/23/28/33/38 sit next to signal pins,
  so an off-by-one jumper often lands on ground.
- Record measured figures (fps, RAM, heap low-water, underruns) in the page for the feature, with the date.

## Next bench tests (agreed 3 Nov 2026)

1. **Audio Pack tone test** — 1 kHz on the **line-out** jack **done 3 Nov 2026**; still to do: L/R ident, scale, volume steps,
   phones jack (PAM8908 amp, gain switch) ([Audio](audio.md#how-to-test-bench)).
2. **Display fps baseline** — re-run test 7 of the display test suite ([Display tests](display-tests.md)) and note the
   figure as the baseline for the UI-performance work.
3. MP3 and ALAC playback (files in `/Music`).
4. Settings persistence (backlight, click feedback).
5. Haptic click per list step.
6. SD recovery: flash mid-playback.
7. Bluetooth scan with the AirPods in pairing mode (expect `audio=yes`).
8. Battery / charging detection (VBUS + CRATE heuristic, [Requirements → Power](requirements.md#power-and-battery)).

## Bluetooth reconnect-on-play (Pico 2 W + AirPods)

For the build with reconnect-on-play ([Bluetooth](bluetooth.md#reconnecting-stage-3-built)). Pair the AirPods once, set Audio output to Auto (or Bluetooth), keep the console open
(`scripts/console.sh`). Lines to watch: `route: playing with no headset stream, waiting (...)`, `bt: reconnecting`, `bt: connect` / `bt: connected`, `bt: a2dp stream open`,
`route: asking the headset to start the stream (n)`, `bt: a2dp streaming`, `bt: a2dp command rejected by the headset (signal 7, start)`, `bt: a2dp start dropped`,
`route: no stream start after 3 requests, closing and reopening it`, and the endings `route: ... , paused`. Toasts: "Connecting to <name>...", "<name> connected", "Couldn't connect to <name> - paused".

1. **Idle hang-up, then Play.** Play a track, pause, wait for the AirPods to hang up (`bt: acl down, reason 0x13`, `bt: link closed ... (idle)`; about 45 s). The output stays Bluetooth and nothing
   connects by itself. Press Play: the toast says "Connecting to ...", the player shows Playing, `bt: reconnecting` then `bt: connected`, `bt: a2dp stream open`, `bt: a2dp streaming`, sound within a few seconds from where it was paused.
2. **Drop mid-track, Play after the reconnect.** Play, walk out of range (or put the AirPods in the closed case with the lid shut for a moment): `Headset disconnected` and the player pauses. Come back, let `Reconnecting...` finish
   (`bt: a2dp stream open`; playback stays paused). Press Play: `bt: a2dp streaming` and sound. **This is bug 2 of 19 Nov:** if it still stays silent, keep the console log from the Play: look for `bt: a2dp command rejected ... start`,
   `route: asking the headset to start the stream (1..3)`, then `route: no stream start after 3 requests, closing and reopening it`; Play must either stream or end in "Couldn't connect to ... - paused", never in silence.
3. **Headset not reachable.** Put the AirPods in the closed case (after the 0x13 hang-up), press Play: "Connecting to ..." toast, the player shows Playing, two `bt: reconnecting` attempts about 20 s + 2 s apart, then
   "Couldn't connect to ... - paused" and `route: could not connect, paused`; the player is paused at the same place, the output is still Bluetooth (nothing from the jack), and no further `bt: reconnecting` follows. Press Play again: it tries again.
4. **Play during "Reconnecting...".** Drop the link, and while the headset row / toast says reconnecting press Play: "Connecting to ..." (it joins the running reconnect, no extra `bt: reconnecting` earlier than the schedule's own), and
   when the AirPods are back (open the case) the track starts by itself: `bt: a2dp streaming`.
5. **Pause while waiting.** In step 3 (case closed) press Pause within the first seconds: no failure toast, the player stays paused, and no second attempt starts (only the one in flight may finish). Also check Disconnect from Settings > Bluetooth
   while waiting: the player pauses with "Headset not connected" and Auto returns to the wired output.
6. **Auto with the output wired.** Boot with the AirPods in the case so the boot retries give up (or Disconnect them): the output is wired; Play plays wired and no `bt: reconnecting` appears.

Record in the page for the feature (with the date) what the log showed for step 2.

## Sleep (Y2)

Steps for the Y2 after flashing a build with [ADR-0021](adr/0021-power-sleep.md). Unplug USB first (a plugged-in Y2 reports external power and never sleeps); watch the log over
the console or `/opt/neupod-logs`. Expect `power: sleep available (suspend to RAM, ...)` at start; if it says the kernel offers no suspend or there is no alarm device, the
player will power off when idle instead (that is the fallback working).

1. Settings > Display > **Sleep after** is 30 min by default; set 10 min for the test. Pause music (or leave it stopped) and do nothing. At 10 min: `bt: controller off for sleep`,
   `ui: backlight off 0%`, `ui: hold on`, `power: going to sleep`; the USB link drops. Press **Power**: the screen comes back dark-locked then lights (`ui: backlight wake`), the log
   (on the card or after reconnecting) shows `power: woke after N ms asleep (key)`; the press does nothing else. Check the picture and the backlight are right, the time slept (N) is about the
   10 minutes, and that it did not go back to sleep by itself (`power: woke` once, no second `going to sleep` until another 10 min).
2. With a headset paired and connected before the sleep: after the wake `bt: controller on after sleep`, `bt: reconnecting`, and "<name> connected" within a minute.
3. Wake with the **wheel** and with a **ring button**: same result (the key is not delivered).
4. Music playing for longer than the Sleep after time: it must not sleep; 10 min after it ends (or is paused) it does.
5. Charger in while asleep (bonus): the wake reason is `external power` if the hardware wakes on it. Charger in while awake: it never sleeps.
6. Kill switch: `NEUPOD_SUSPEND=0` in `/etc/neupod-fw.env`, restart the firmware: the start-up line says sleep is switched off and, after the idle time, `power: idle for the sleep time, powering off`.
7. Measure the current in suspend if you can (the point of the feature), and note how long the sleep and the wake take.
8. 24 h limit (optional, long): leave it asleep for a day. It should be off by then (not suspended), and the next power-up should come back at the saved position.
9. After a wake, play a file and browse a folder: no SD I/O errors in the log (the card must be re-detected across the suspend), and the player carries on.
10. Kill the firmware while it is asleep or just requesting sleep (`kill` by PID only), let the wrapper restart it: the new start writes `on` and the screen must be lit (the panel not left early-suspended).
11. Timer wake: set `NEUPOD_SLEEP_MAX_S=120` in `/etc/neupod-fw.env`, restart, let it sleep: after 2 minutes it must wake with `(timer)` and power off. Check **both clocks** in the log: the line `woke after N ms asleep` (CLOCK_BOOTTIME against CLOCK_MONOTONIC) and that the timer verdict also holds if N is 0 (the real-time clock fallback); note which of the two decided.
12. Inspect what a wheel touch produces as the wake (the log line says key; check the key is not delivered and nothing opens), and what Power does.
13. Charger plug-in while asleep: does the hardware wake on it (`external power` reason)? If not, the unit sleeps on until a key; that is acceptable.

## Hold (Y2)

Steps for the Y2 after flashing a build with [ADR-0020](adr/0020-hold.md) (watch `/opt/neupod-logs` or the console for the `ui: hold`, `ui: backlight` and
`y2: power held` lines):

1. **Tap Power** (quick): the backlight goes off (`ui: backlight off 0%`) and `ui: hold on` follows (Auto-lock is On by default). Turn the wheel or press anything: the screen
   wakes, shows the pill "Locked · hold Power 2 s to unlock", and nothing else happens (no click, no Play/Pause, no selection moved).
2. **Hold Power**: after about 0.4 s a blue line grows under the status bar and the pill says "Keep holding to unlock · 10 s = off"; let go at 1 s: nothing changes. Hold again to 2 s: "Unlocked", one haptic click, the lock icon
   leaves the status bar, `ui: hold off`. Keys work again.
3. **Hold Power to 2 s and on**: "Locked" appears at 2 s; at 5 s the line turns red and the pill counts "Release to cancel · shutting down in 5"; **let go at 7 s: nothing powers off** (and nothing
   is lost). Repeat and hold to 10 s: `ui: power-off requested`, `y2: power held, shutting down`, the storage task stops, the player exits with status 77 and the device powers
   off (the wrapper's `poweroff did not happen, restarting` must NOT appear).
4. With music playing and the screen locked: the headset's buttons (play/pause, next, previous) still work; the wheel and the ring buttons do not.
5. Settings > Display: **Screen off after** (try 15 s) and **Auto-lock** Off: the screen goes off with Hold left as it was; the waking press is lost, the next one works.
6. Charging-only mode (USB with the player "off") is unchanged: Power held 2 s reboots.
