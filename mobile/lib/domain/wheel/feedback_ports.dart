// SPDX-License-Identifier: MPL-2.0

/// The haptic events of the device view design (section 5, Haptics).
enum FeedbackKind {
  /// Each list step, volume step or seek step.
  detent,

  /// A button or the centre went down.
  buttonDown,

  /// A button or the centre came up.
  buttonUp,

  /// A hold took effect (scan starts, seek mode entered).
  holdTookEffect,

  /// The end of a list was reached (once).
  endReached,
}

/// Plays haptics. The app's implementation is the in-repo platform plugin;
/// tests and platforms without haptics use [SilentHaptics].
abstract interface class HapticsPort {
  /// Gets the generators ready so the first tick is not late (iOS); call when
  /// the wheel is touched.
  void prepare();

  /// Plays [kind]. The platform decides whether to play it (the system
  /// haptics setting).
  void play(FeedbackKind kind);
}

/// Plays click sounds (the Click sound setting, Off by default). A seam: R1's
/// phase 2 ships only [SilentClicks].
abstract interface class ClickSoundPort {
  /// Plays the tick for a detent or the click for a press.
  void play(FeedbackKind kind);
}

/// No haptics.
class SilentHaptics implements HapticsPort {
  /// No haptics.
  const SilentHaptics();

  @override
  void prepare() {}

  @override
  void play(FeedbackKind kind) {}
}

/// No click sounds.
class SilentClicks implements ClickSoundPort {
  /// No click sounds.
  const SilentClicks();

  @override
  void play(FeedbackKind kind) {}
}
