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

## Hold (Y2)

Steps for the Y2 after flashing a build with [ADR-0020](adr/0020-hold.md) (watch `/opt/neupod-logs` or the console for the `ui: hold`, `ui: backlight` and
`y2: power held` lines):

1. **Tap Power** (quick): the backlight goes off (`ui: backlight off 0%`) and `ui: hold on` follows (Auto-lock is On by default). Turn the wheel or press anything: the screen
   wakes, shows "Locked, Hold Power to unlock", and nothing else happens (no click, no Play/Pause, no selection moved).
2. **Hold Power**: after about 0.4 s a bar fills under "Hold to unlock"; let go at 1 s: nothing changes. Hold again to 2 s: "Unlocked", one haptic click, the lock glyph
   leaves the status bar, `ui: hold off`. Keys work again.
3. **Hold Power to 2 s and on**: "Locked" appears at 2 s; at 5 s "Powering off in 5" counts down with a draining bar; **let go at 7 s: nothing powers off** (and nothing
   is lost). Repeat and hold to 10 s: `ui: power-off requested`, `y2: power held, shutting down`, the storage task stops, the player exits with status 77 and the device powers
   off (the wrapper's `poweroff did not happen, restarting` must NOT appear).
4. With music playing and the screen locked: the headset's buttons (play/pause, next, previous) still work; the wheel and the ring buttons do not.
5. Settings > Display: **Screen off after** (try 15 s) and **Auto-lock** Off: the screen goes off with Hold left as it was; the waking press is lost, the next one works.
6. Charging-only mode (USB with the player "off") is unchanged: Power held 2 s reboots.
