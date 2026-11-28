// SPDX-License-Identifier: MPL-2.0

/// What Custom mode needs to know about the phone's accessibility settings
/// that Flutter does not expose. Pure Dart; the platform implements it
/// (`device_platform.dart`), tests keep the silent default.
library;

/// The system's Reduce Transparency setting (design section 8).
abstract interface class AccessibilityPort {
  /// Whether Reduce Transparency is on now.
  Future<bool> reduceTransparency();

  /// The new value each time the user changes it.
  Stream<bool> reduceTransparencyChanges();
}

/// Reduce Transparency is never on: the default, and Android's answer.
class NoAccessibilitySettings implements AccessibilityPort {
  const NoAccessibilitySettings();

  @override
  Future<bool> reduceTransparency() async => false;

  @override
  Stream<bool> reduceTransparencyChanges() => const Stream.empty();
}
