# ui/device: the device view

The app is the device (owner decision 9). This folder draws the front of a neuPod and hosts the screens in its 4:3
screen. Everything is built from the device view design (`docs/design/2026-11-21-app-device-view/`), whose `render.py`
is the reference for the layout (`test/ui/device/device_geometry_test.dart` compares against it).

| File | What |
| --- | --- |
| `device_view.dart` | `DeviceView(screen: ...)`: body, frame, screen, wordmark, wheel; status bar style; back gesture = Menu; long press on the body opens the gate switches |
| `device_geometry.dart` | Every position from W, the phone's width: fit A, minimum width, wheel shrink, wordmark rule |
| `device_paint.dart` | `DevicePaint`: everything the painters read (gradients, edge, groove, ink, tints, status-bar ink, opacities and cap/badge/board flags). The one paint type |
| `device_finish.dart` | Silver and black, the finish table's colours, as a `DevicePaint` (`finish.paint`) |
| `custom_device_paint.dart` | `customDevicePaint(look)`: a custom look (and the Increase Contrast flag) to a `DevicePaint`, by the contrast rule in `domain/style/custom_paint.dart`. Solid parts only for now |
| `device_options.dart` | The switchable choices (direct touch, status bar, wordmark, progress-bar seek, iOS edge swipe, haptics, click sound) |
| `click_wheel.dart` | The wheel widget (touch, haptics, the screen reader's adjustable element) and `WheelKeyboard` |
| `wheel_painter.dart` | The wheel's drawing, five states |
| `wheel_bus.dart` | `wheelBusProvider`: events out to the screen; semantics, `atRoot`, `enabled` and hold times in from it |
| `screen_host.dart` | `DeviceScreenHost` (600x450, scaled by s) and `DeviceScreenScope` |
| `device_feedback.dart`, `system_volume.dart` | Haptics, click-sound seam and system volume, behind providers that `app/` overrides with the `neupod_device` plugin |
| `gate_panel.dart` | The design gate's switches (temporary) |
| `placeholder_screen.dart` | A stand-in screen until the real ones land |

## For the screens (UI engineer 1)

- **Size.** Build each screen at 600x450 design pixels; `DeviceScreenHost` scales it. Do not read the phone's size.
- **Wheel.** Listen to `ref.read(wheelBusProvider).events` (see `lib/domain/wheel/README.md` for the events). When a
  screen shows, it sets the following, and removes what it added when it goes:
  - `bus.semantics`: label and value as in design section 9. The label defaults to `Click wheel`, and lists keep it.
    Set `playing` from the player's state (`playerStateProvider`, decision 0006, when the audio item lands), so the
    fifth action reads Play or Pause.
  - `bus.atRoot`: true only on Home.
  - `bus.holdMs`: none by default. Now Playing adds `WheelInput.nowPlayingHoldMs`, and drops the centre in seek mode.
    Search adds Menu at 600 ms.
  - `bus.screenGivesHoldHaptic`: Now Playing adds Previous, Next and the centre, and gives the hold haptic from
    `NowPlayingControl`.
- **Haptics.** `ref.read(deviceFeedbackProvider).detent()` for each step that moved something;
  `endReached()` once when a list is pushed past its end. The wheel gives the button haptics itself.
- **Direct touch** is the wheel plus taps and drags on the screen (owner decision 18). Pointer events reach the screen in
  its 600x450 coordinates. A row tap moves the selection there and selects it, with the pressed tint from touch-down.
  A list drag scrolls and keeps the selection on the nearest row. A Cover Flow drag moves the covers; a tap on the
  centre cover opens it. A tap on Now Playing's art opens page 2. The status bar, the right-hand pane and panel
  backgrounds take no touches; a panel's `OK` can be tapped.
- **Seeking** is by the wheel's seek mode only: the progress bar is not draggable (owner decision 18).
- **Screen-reader rows.** Each row is one element: `Semantics(label: 'Albums, 4 of 7', selected: ..., button: true,
  onTap: open, excludeSemantics: true, child: ...)`, as `placeholder_screen.dart` does. Rows keep the `onTap` so the screen reader can open them.
- **Text size.** The host passes the phone's `MediaQuery` through unchanged, so the screen stays 600x450 whatever the
  setting. Screens read `MediaQuery.textScalerOf(context)` and apply the design's k tiers themselves (section 8:
  7, 6 or 5 rows). The body's wordmark never scales.
- **Reduce Motion.** When `MediaQuery.disableAnimationsOf(context)` is true, the selection bar and screen changes do
  not animate (design section 9).
- **Fast scroll.** Use `LetterJump` (the firmware's trigger: 6 accelerated steps with no gap over 400 ms; hide after
  250 ms at rest, then a 150 ms fade).
- **Volume.** `systemVolumeProvider`; see the comment there.
- **Home items** are the specification's (Music, Playlists, Settings, Shuffle Songs, Now Playing), and the status bar
  shows no Wi-Fi in R1 (design section 10), whatever some mock-ups draw.

## The phone's status bar

`DeviceView` sets the system UI mode once at start and again only when the Status bar switch changes, never from
`build`. It restores edge to edge when it goes. Hiding the bar keeps the notch or Dynamic Island space reserved, but on
a phone without one the top inset drops to 0, so the whole device moves up by the bar's height (about 20 pt). That is
expected; the geometry follows the inset.

## Gate switches in store builds

The long press that opens the gate switches is compiled in unless the build passes `--dart-define=NEUPOD_GATE=false`.
Store builds should pass it. The long press is hidden from the screen reader, and while the panel is open the keyboard
drives the panel, not the wheel.

## Custom mode additions (AP-1, 3/3)

- `edge_pulse.dart`: `edgePulseProvider` and `applyEdgePulse`. The part editor lights the edge of the part it just switched to (400 ms, up to full ink
  and back; never under Reduce Motion); `DeviceView` applies it to the paint (`DevicePaint.withEdges`).
- `DeviceView` passes `MediaQuery.highContrastOf(context)` to `customDevicePaint(increaseContrast:)`: Increase Contrast paints a look solid at 7:1.
- Reduce Transparency has no Flutter flag: `app/accessibility_providers.dart`'s `reduceTransparencyProvider` is the one place it is read (false until the
  transparency step wires the platform flag in).
