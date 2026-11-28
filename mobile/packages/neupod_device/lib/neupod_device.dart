// SPDX-License-Identifier: MPL-2.0

/// neuPod's device feel on the phone: the click wheel's haptics and the
/// system media volume. An in-repo plugin (Kotlin and Swift through Pigeon);
/// the platform APIs and why are in docs/app/decisions/0007-haptics.md.
library;

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:neupod_device/src/device_api.g.dart';

export 'package:neupod_device/src/device_api.g.dart'
    show HapticEvent, VolumeState, VolumeStatus;

/// The plugin's Dart face.
class NeupodDevice {
  /// The plugin; [api] is for tests.
  NeupodDevice({DeviceApi? api}) : _api = api ?? DeviceApi();

  final DeviceApi _api;

  static const MethodChannel _accessibility = MethodChannel(
    'neupod_device/accessibility',
  );

  /// Gets the haptic generators ready (iOS).
  Future<void> prepareHaptics() => _api.prepareHaptics();

  /// Plays [event].
  Future<void> playHaptic(HapticEvent event) => _api.playHaptic(event);

  /// Whether the system's Reduce Transparency is on. iOS reads it; Android
  /// has no such setting and always answers false.
  Future<bool> reduceTransparency() async =>
      (await _accessibility.invokeMethod<bool>('reduceTransparency')) ?? false;

  /// The new value each time the user changes Reduce Transparency (iOS).
  Stream<bool> reduceTransparencyChanges() {
    final changes = StreamController<bool>.broadcast();
    _accessibility.setMethodCallHandler((call) async {
      if (call.method == 'reduceTransparencyChanged') {
        changes.add(call.arguments == true);
      }
    });
    return changes.stream;
  }

  /// The media volume now.
  Future<VolumeState> volume() => _api.getVolume();

  /// One platform volume step up ([direction] > 0) or down: the volume read
  /// back from the system afterwards, with whether it moved.
  Future<VolumeState> stepVolume(int direction) =>
      _api.stepVolume(direction.sign);
}
