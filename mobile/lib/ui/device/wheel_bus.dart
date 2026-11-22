// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';

/// What the screen reader says about the wheel (design section 9): one
/// adjustable element whose label and value the current screen sets.
@immutable
class WheelSemantics {
  /// A description of the wheel for the screen reader.
  const WheelSemantics({
    this.label = 'Click wheel',
    this.value = '',
    this.increasedValue = '',
    this.decreasedValue = '',
    this.playing = false,
  });

  /// `Click wheel` in lists and menus, `Volume` on Now Playing, `Seek` in
  /// seek mode.
  final String label;

  /// The focused row (`Albums, 4 of 7`), the volume (`62 percent`) or the
  /// seek position.
  final String value;

  /// What [value] becomes after Increment, if known.
  final String increasedValue;

  /// What [value] becomes after Decrement, if known.
  final String decreasedValue;

  /// Something is playing: the fifth action is Pause, else Play.
  final bool playing;

  @override
  bool operator ==(Object other) =>
      other is WheelSemantics &&
      other.label == label &&
      other.value == value &&
      other.increasedValue == increasedValue &&
      other.decreasedValue == decreasedValue &&
      other.playing == playing;

  @override
  int get hashCode =>
      Object.hash(label, value, increasedValue, decreasedValue, playing);
}

/// Connects the click wheel to the screen inside the device: the wheel
/// [emit]s, screens listen to [events] and describe themselves through
/// [semantics], [atRoot], [enabled] and [holdMs].
///
/// Every input path produces the same events: touch on the wheel, the screen
/// reader's adjustable actions, a keyboard or switch control, and the system
/// back gesture (as a Menu press).
class WheelBus {
  final StreamController<WheelEvent> _events =
      StreamController<WheelEvent>.broadcast(sync: true);

  /// The wheel's events, for the current screen.
  Stream<WheelEvent> get events => _events.stream;

  /// Sends [event] to the screens.
  void emit(WheelEvent event) {
    if (!_events.isClosed) _events.add(event);
  }

  /// A complete press from a source without touch timing (a keyboard key, a
  /// screen-reader action, the back gesture): down, up and press.
  void press(WheelButton button) {
    emit(WheelButtonDown(button));
    emit(WheelButtonUp(button));
    emit(WheelPress(button));
  }

  /// What the screen reader says about the wheel. The current screen sets it.
  final ValueNotifier<WheelSemantics> semantics = ValueNotifier(
    const WheelSemantics(),
  );

  /// False while the wheel cannot act (design section 4, Disabled): drawn
  /// disabled, touches ignored.
  final ValueNotifier<bool> enabled = ValueNotifier(true);

  /// The current screen is Home: Android's back leaves the app there; on iOS
  /// the left-edge swipe does nothing.
  final ValueNotifier<bool> atRoot = ValueNotifier(true);

  /// The hold times of the buttons, shared with the wheel. Empty by default
  /// (every press is a plain press); screens add what they act on: Now
  /// Playing `WheelInput.nowPlayingHoldMs` (leaving the centre out in seek
  /// mode), Search Menu at 600 ms. Remove them when the screen goes.
  final Map<WheelButton, int> holdMs = Map.of(WheelInput.defaultHoldMs);

  /// Buttons whose hold haptic the screen gives itself, so the wheel gives
  /// none on [WheelHoldStart]. Now Playing adds Previous, Next and the
  /// centre: its haptic comes from `NowPlayingControl` (seek mode entered,
  /// a scan started on a seekable track), never on a track that cannot seek.
  final Set<WheelButton> screenGivesHoldHaptic = {};

  /// Closes the stream.
  void dispose() {
    _events.close();
    semantics.dispose();
    enabled.dispose();
    atRoot.dispose();
  }
}

/// The wheel bus of the device view.
final wheelBusProvider = Provider<WheelBus>((ref) {
  final bus = WheelBus();
  ref.onDispose(bus.dispose);
  return bus;
});

/// The wheel's clock: monotonic milliseconds. Tests override it.
final wheelClockProvider = Provider<int Function()>((ref) {
  final watch = Stopwatch()..start();
  return () => watch.elapsedMilliseconds;
});
