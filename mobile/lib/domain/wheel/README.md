# domain/wheel: the click wheel's logic

Pure Dart. The device view (`ui/device/`) feeds touch samples in; screens consume `WheelEvent`s. Built from the device
view design (`docs/design/2026-11-21-app-device-view/`, sections 4 and 5) and ports of the firmware's own code.

| File | What | Source |
| --- | --- | --- |
| `firmware_constants.dart` | Wheel constants | `hal_input.h`, `runtime_config.h`, `ui_logic.h`, `wheel_input.h`, `ui_rail.c`; checked against the headers by a test |
| `wheel_accel.dart` | Acceleration | Exact port of `ui_accel_feed()` (`app/ui/ui_logic.c`) |
| `letter_jump.dart` | When the fast-scroll letter tile and rail show | Exact port of `wi_letterjump_*` (`app/ui/input/wheel_input.c`) |
| `now_playing_control.dart` | Now Playing: volume or seek mode, Select hold, Previous / Next scan | Exact port of `ui_npc_*` (`app/ui/ui_logic.c`) |
| `wheel_input.dart` | Touch to events: hit zones, press or turn, holds, first touch owns the wheel | The design, sections 4 and 5 |
| `wheel_events.dart` | The event types screens consume | |
| `rate_limiter.dart` | At most 30 haptic ticks or clicks a second | The design, section 5 |

The ports are checked against the C two ways: the firmware's own unit tests, case for case
(`test/domain/wheel/*_test.dart`, same names), and vectors made by the C functions compiled on the host
(`tool/wheel_vectors/gen_wheel_vectors.sh`, replayed by `firmware_vectors_test.dart`). Rerun the script when the firmware
changes.

## For screens

- `WheelStep(steps, multiplier)`: move `steps` rows (already accelerated; positive is clockwise, down a list).
- `WheelPress(button)`: Select (`centre`), Menu, Previous, Next, Play/Pause, on release of a press that did not become a
  turn.
- `WheelHoldStart(button)` / `WheelHoldEnd(button)`: Previous and Next after 600 ms; the centre after 500 ms only where
  the screen adds it to `WheelInput.holdMs` (Now Playing).
- `WheelButtonDown` / `WheelButtonUp` / `WheelButtonCancelled`: the raw press, for Now Playing, which drives
  `NowPlayingControl` exactly as the firmware's Now Playing drives `ui_npc_t`: Select down and up, Previous / Next as
  `keyDown(-1 / +1)` and `keyUp`, a cancelled Previous / Next as `cancelHold()`, a cancelled Select as `selectUp()` with
  its result ignored, and `tick()` every frame.
- A detent haptic belongs to a step that actually moved something; at the end of a list give the end haptic once.

## The fast-scroll trigger (what the firmware does)

Not the multiplier. `ui_library.c` and `ui_files.c` call `wi_letterjump_scroll()` with every move of a long sorted list
(the accelerated steps, and the initial of the newly selected row's sort key). The overlay shows once **6 or more steps**
have come with **no gap over 400 ms** between moves, stays while moves keep coming, is hidden by `wi_letterjump_tick()`
once **more than 250 ms** pass without a move, and `ui_rail.c` fades it out over **150 ms** (ease-in); a move during the
fade shows it at full strength again. In the firmware it is used on the Library's top-level lists and in Files.
`LetterJump` is that logic; call `scroll()` when the list moves and `tick()` every frame.

## Acceleration and the touch sample rate

`ui_accel_feed()` decays its energy in integers, so how far a turn accelerates depends on how often positions are fed,
not only on speed. The wheel feeds on every touch sample (whole positions only, the fraction kept). `dart run
tool/wheel_vectors/accel_speeds.dart` prints the result; at the time of writing:

| deg/s | 150 | 300 | 450 | 600 | 800 | 1000 | 1500 | 2000 | 3000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 60 Hz touch | 1x | 1x | 1x | 2x | 4x | 4x | 8x | 8x | 8x |
| 120 Hz touch | 1x | 1x | 1x | 2x | 2x | 4x | 8x | 8x | 8x |
| 240 Hz touch | 1x | 1x | 1x | 1x | 1x | 4x | 4x | 8x | 8x |

The design's "roughly at a steady speed" column (2x from 300 degrees a second) assumed the energy is a plain count over
150 ms; with the firmware's linear decay a steady turn reaches about half that, so 2x arrives near 600 degrees a second.
