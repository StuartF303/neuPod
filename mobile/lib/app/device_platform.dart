// SPDX-License-Identifier: MPL-2.0

/// The device view's platform services, from the in-repo `neupod_device`
/// plugin: haptics and the system volume. `main.dart` overrides the UI's
/// providers with these; tests keep the silent defaults.
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:neupod/domain/style/accessibility_port.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/ui/device/device_accessibility.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/system_volume.dart';
import 'package:neupod_device/neupod_device.dart';

/// Haptics through the plugin. Calls are fire-and-forget: a haptic that
/// fails (no window yet, a platform without haptics) must never disturb the
/// wheel.
class PlatformHaptics implements HapticsPort {
  /// Haptics through [device].
  PlatformHaptics(this.device);

  /// The plugin.
  final NeupodDevice device;

  /// The event for each feedback kind (decision 0007 maps them to APIs).
  static HapticEvent eventFor(FeedbackKind kind) => switch (kind) {
    FeedbackKind.detent => HapticEvent.detent,
    FeedbackKind.buttonDown => HapticEvent.buttonDown,
    FeedbackKind.buttonUp => HapticEvent.buttonUp,
    FeedbackKind.holdTookEffect => HapticEvent.holdTookEffect,
    FeedbackKind.endReached => HapticEvent.endReached,
  };

  @override
  void prepare() => unawaited(device.prepareHaptics().catchError(_ignore));

  @override
  void play(FeedbackKind kind) =>
      unawaited(device.playHaptic(eventFor(kind)).catchError(_ignore));

  static void _ignore(Object e) {
    debugPrint('neupod_device: $e');
  }
}

/// The system volume through the plugin.
class PlatformSystemVolume implements SystemVolumePort {
  /// The volume through [device].
  PlatformSystemVolume(this.device);

  /// The plugin.
  final NeupodDevice device;

  static SystemVolume _of(VolumeState v) => SystemVolume(
    step: v.step,
    steps: v.steps,
    fixed: v.fixed,
    status: switch (v.status) {
      VolumeStatus.ok => SystemVolumeStatus.ok,
      VolumeStatus.noSlider => SystemVolumeStatus.noSlider,
      VolumeStatus.noEffect => SystemVolumeStatus.noEffect,
      VolumeStatus.fixed => SystemVolumeStatus.fixed,
    },
  );

  @override
  Future<SystemVolume> read() async => _of(await device.volume());

  @override
  Future<SystemVolume> step(int direction) async =>
      _of(await device.stepVolume(direction));
}

/// Reduce Transparency through the plugin (iOS reads UIAccessibility; Android
/// has no such setting and answers false).
class PlatformAccessibility implements AccessibilityPort {
  /// Settings through [device].
  PlatformAccessibility(this.device);

  /// The plugin.
  final NeupodDevice device;

  @override
  Future<bool> reduceTransparency() => device.reduceTransparency();

  @override
  Stream<bool> reduceTransparencyChanges() =>
      device.reduceTransparencyChanges();
}

/// The overrides `main.dart` installs on a phone.
List<Override> devicePlatformOverrides() {
  final device = NeupodDevice();
  return [
    hapticsPortProvider.overrideWithValue(PlatformHaptics(device)),
    systemVolumeProvider.overrideWithValue(PlatformSystemVolume(device)),
    accessibilityPortProvider.overrideWithValue(PlatformAccessibility(device)),
  ];
}
