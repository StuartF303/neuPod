// SPDX-License-Identifier: MPL-2.0

/// The click wheel's five controls.
enum WheelButton {
  /// Top of the ring: back one level.
  menu,

  /// Right of the ring: next track.
  next,

  /// Bottom of the ring: play or pause.
  playPause,

  /// Left of the ring: previous track.
  previous,

  /// The centre button: Select.
  centre,
}

/// What the wheel tells the screens. Screens consume a stream of these.
///
/// A press produces, in order: [WheelButtonDown] at touch-down (show it, click
/// the haptic), then either [WheelButtonCancelled] (it became a turn, the
/// finger slid off the centre, or the system took the touch: do nothing), or
/// [WheelButtonUp] followed by [WheelPress] (act), or, for a button with a
/// hold time, [WheelHoldStart] once held long enough and [WheelButtonUp] then
/// [WheelHoldEnd] on release (no [WheelPress]). A hold that ends without a
/// release (the system took the touch, the screen changed, the wheel was
/// disabled) gives [WheelButtonCancelled] then [WheelHoldEnd]: whatever the
/// hold started must not be applied (Now Playing calls
/// `NowPlayingControl.cancelHold()`, as the firmware does).
///
/// Most screens need only [WheelStep], [WheelPress], [WheelHoldStart] and
/// [WheelHoldEnd]. Now Playing feeds the raw down / up / cancel events to
/// `NowPlayingControl`, as the firmware does.
sealed class WheelEvent {
  const WheelEvent();
}

/// The wheel turned: move [steps] list steps (already accelerated; positive is
/// clockwise: down a list, volume up, seek forward). [multiplier] is the
/// acceleration (1, 2, 4 or 8) the firmware's `ui_accel_feed()` applied.
final class WheelStep extends WheelEvent {
  /// A step event.
  const WheelStep(this.steps, this.multiplier);

  /// Signed list steps.
  final int steps;

  /// 1, 2, 4 or 8.
  final int multiplier;

  @override
  bool operator ==(Object other) =>
      other is WheelStep &&
      other.steps == steps &&
      other.multiplier == multiplier;

  @override
  int get hashCode => Object.hash(steps, multiplier);

  @override
  String toString() => 'WheelStep($steps, x$multiplier)';
}

/// A button went down (shown and clicked at once, before it is known to be a
/// press).
final class WheelButtonDown extends WheelEvent {
  /// A down event.
  const WheelButtonDown(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelButtonDown && other.button == button;

  @override
  int get hashCode => Object.hash(WheelButtonDown, button);

  @override
  String toString() => 'WheelButtonDown(${button.name})';
}

/// A button was released after a press or a hold.
final class WheelButtonUp extends WheelEvent {
  /// An up event.
  const WheelButtonUp(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelButtonUp && other.button == button;

  @override
  int get hashCode => Object.hash(WheelButtonUp, button);

  @override
  String toString() => 'WheelButtonUp(${button.name})';
}

/// A press that will not act: it became a turn, slid off the centre, or the
/// touch was cancelled. No release haptic.
final class WheelButtonCancelled extends WheelEvent {
  /// A cancel event.
  const WheelButtonCancelled(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelButtonCancelled && other.button == button;

  @override
  int get hashCode => Object.hash(WheelButtonCancelled, button);

  @override
  String toString() => 'WheelButtonCancelled(${button.name})';
}

/// A short press: act on it (Select, Menu, Previous, Next, Play/Pause).
final class WheelPress extends WheelEvent {
  /// A press event.
  const WheelPress(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelPress && other.button == button;

  @override
  int get hashCode => Object.hash(WheelPress, button);

  @override
  String toString() => 'WheelPress(${button.name})';
}

/// A hold took effect (Previous / Next after 600 ms; the centre after 500 ms
/// where the screen asks for it, i.e. Now Playing).
final class WheelHoldStart extends WheelEvent {
  /// A hold-start event.
  const WheelHoldStart(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelHoldStart && other.button == button;

  @override
  int get hashCode => Object.hash(WheelHoldStart, button);

  @override
  String toString() => 'WheelHoldStart(${button.name})';
}

/// A hold ended (the finger lifted, or the touch was cancelled).
final class WheelHoldEnd extends WheelEvent {
  /// A hold-end event.
  const WheelHoldEnd(this.button);

  /// Which control.
  final WheelButton button;

  @override
  bool operator ==(Object other) =>
      other is WheelHoldEnd && other.button == button;

  @override
  int get hashCode => Object.hash(WheelHoldEnd, button);

  @override
  String toString() => 'WheelHoldEnd(${button.name})';
}
