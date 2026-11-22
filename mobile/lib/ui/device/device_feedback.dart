// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/rate_limiter.dart';
import 'package:neupod/ui/device/device_options.dart';

/// The platform haptics. The composition root (`app/`) overrides it with the
/// in-repo plugin; by default the app is silent (tests, goldens).
final hapticsPortProvider = Provider<HapticsPort>(
  (ref) => const SilentHaptics(),
);

/// The click sound. A seam: silent until the Click sound item lands.
final clickSoundPortProvider = Provider<ClickSoundPort>(
  (ref) => const SilentClicks(),
);

/// A monotonic clock in microseconds, for the 30-a-second limit. Tests
/// override it.
final feedbackClockProvider = Provider<int Function()>((ref) {
  final watch = Stopwatch()..start();
  return () => watch.elapsedMicroseconds;
});

/// Haptics and click sounds for the device view, with the design's rules
/// (section 5): detents limited to 30 a second with the
/// rest dropped; nothing when the in-app Haptics setting is off; the click
/// sound only when that setting is on. The system haptics setting is obeyed
/// by the platform APIs themselves (decision 0007).
///
/// Who calls what:
///
/// - the wheel widget: [buttonDown], [buttonUp], [holdTookEffect];
/// - screens: [detent] for each step that actually moved something (a row,
///   the volume, the seek target), [endReached] once when a list is at its
///   end and the wheel pushes past it.
class DeviceFeedback {
  /// Feedback through [haptics] and [clicks].
  DeviceFeedback({
    required this.haptics,
    required this.clicks,
    required this.options,
    required this.nowUs,
  });

  /// The platform haptics.
  final HapticsPort haptics;

  /// The click sound.
  final ClickSoundPort clicks;

  /// The current options (read at each call).
  final DeviceViewOptions Function() options;

  /// Microseconds, monotonic.
  final int Function() nowUs;

  final RateLimiter _ticks = RateLimiter();

  /// The wheel was touched: get ready.
  void prepare() {
    if (options().haptics) haptics.prepare();
  }

  /// A list, volume or seek step.
  void detent() {
    if (!_ticks.allow(nowUs())) return;
    _play(FeedbackKind.detent);
  }

  /// A button or the centre went down.
  void buttonDown() => _play(FeedbackKind.buttonDown);

  /// A button or the centre came up (not after a cancelled press).
  void buttonUp() => _play(FeedbackKind.buttonUp);

  /// A hold took effect.
  void holdTookEffect() => _play(FeedbackKind.holdTookEffect);

  /// The end of a list, once.
  void endReached() => _play(FeedbackKind.endReached);

  void _play(FeedbackKind kind) {
    final o = options();
    if (o.haptics) haptics.play(kind);
    if (o.clickSound && kind != FeedbackKind.buttonUp) clicks.play(kind);
  }
}

/// The device view's feedback.
final deviceFeedbackProvider = Provider<DeviceFeedback>((ref) {
  return DeviceFeedback(
    haptics: ref.watch(hapticsPortProvider),
    clicks: ref.watch(clickSoundPortProvider),
    options: () => ref.read(deviceViewOptionsProvider),
    nowUs: ref.watch(feedbackClockProvider),
  );
});
