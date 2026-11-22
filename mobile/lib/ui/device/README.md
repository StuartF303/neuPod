# ui/device: the device view

The app is the device (owner decision 9). This folder draws the front of a neuPod and hosts the screens in its 4:3
screen. Everything is built from the device view design (`docs/design/2026-11-21-app-device-view/`), whose `render.py`
is the reference for the layout (`test/ui/device/device_geometry_test.dart` compares against it).

| File | What |
| --- | --- |
| `device_view.dart` | `DeviceView(screen: ...)`: body, frame, screen, wordmark, wheel; status bar style; back gesture = Menu; long press on the body opens the gate switches |
| `device_geometry.dart` | Every position from W, the phone's width: fit A and B, minimum width, wheel shrink, wordmark rule |
| `device_finish.dart` | Silver and black, the finish table's colours |
| `device_options.dart` | The switchable choices (fit, body, direct touch, status bar, wordmark, progress-bar seek, iOS edge swipe, haptics, click sound) |
| `click_wheel.dart` | The wheel widget (touch, haptics, the screen reader's adjustable element) and `WheelKeyboard` |
| `wheel_painter.dart` | The wheel's drawing, five states |
| `wheel_bus.dart` | `wheelBusProvider`: events out to the screen; semantics, `atRoot`, `enabled` and hold times in from it |
| `screen_host.dart` | `DeviceScreenHost` (600x450, scaled by s) and `DeviceScreenScope` |
| `device_feedback.dart`, `system_volume.dart` | Haptics, click-sound seam and system volume, behind providers that `app/` overrides with the `neupod_device` plugin |
| `gate_panel.dart` | The design gate's switches (temporary) |
| `placeholder_screen.dart` | A stand-in screen until the real ones land |

## For the screens (UI engineer 1)

- **Size.** Build each screen at 600x450 design pixels; `DeviceScreenHost` scales it. Do not read the phone's size.
- **Wheel.** Listen to `ref.read(wheelBusProvider).events` (see `lib/domain/wheel/README.md` for the events). When the
  screen shows, set `bus.semantics` (label and value as design section 9), `bus.atRoot` (true only on Home) and, on Now
  Playing, `bus.holdMs[WheelButton.centre] = uiSelHoldMs`. Remove the centre hold when Now Playing goes.
- **Haptics.** `ref.read(deviceFeedbackProvider).detent()` for each step that moved something;
  `endReached()` once when a list is pushed past its end. The wheel gives the button haptics itself.
- **Direct touch.** `DeviceScreenScope.of(context).directTouch`:
  - Candidate 1 (`wheelOnly`): the host takes all pointer hits away from the screen. Semantics stay, so rows must
    still have a semantics `onTap` that opens them.
  - Candidate 2 (`wheelAndScreen`): pointer events reach the screen in its 600x450 coordinates. A row tap moves the
    selection there and selects it, with the pressed tint from touch-down. A list drag scrolls and keeps the selection
    on the nearest row. A Cover Flow drag moves the covers; a tap on the centre cover opens it. A tap on Now Playing's
    art opens page 2. The status bar, the right-hand pane and panel backgrounds take no touches; a panel's `OK` can be
    tapped.
- **Seek by drag.** `DeviceScreenScope.of(context).dragProgressToSeek`: off by default (the design). When on, Now
  Playing's progress bar may be dragged (the specification's wording); the owner chooses at the gate.
- **Fast scroll.** Use `LetterJump` (the firmware's trigger: 6 accelerated steps with no gap over 400 ms; hide after
  250 ms at rest, then a 150 ms fade).
- **Volume.** `systemVolumeProvider`; see the comment there.
- **Home items** are the specification's (Music, Playlists, Settings, Shuffle Songs, Now Playing), and the status bar
  shows no Wi-Fi in R1 (design section 10), whatever some mock-ups draw.
