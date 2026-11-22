// SPDX-License-Identifier: MPL-2.0

// The interface between Dart and the platforms. After editing, from
// mobile/packages/neupod_device run:
//   dart run pigeon --input pigeons/device_api.dart
// and commit the generated files.

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(
  PigeonOptions(
    dartPackageName: 'neupod_device',
    dartOut: 'lib/src/device_api.g.dart',
    kotlinOut:
        'android/src/main/kotlin/net/stuartfraser/neupod_device/DeviceApi.g.kt',
    kotlinOptions: KotlinOptions(package: 'net.stuartfraser.neupod_device'),
    swiftOut: 'ios/neupod_device/Sources/neupod_device/DeviceApi.g.swift',
    copyrightHeader: 'pigeons/copyright.txt',
  ),
)
/// The haptic events of the device view design (section 5).
enum HapticEvent {
  /// A list, volume or seek step.
  detent,

  /// A button or the centre went down.
  buttonDown,

  /// A button or the centre came up.
  buttonUp,

  /// A hold took effect.
  holdTookEffect,

  /// The end of a list was reached.
  endReached,
}

/// The phone's media volume.
class VolumeState {
  VolumeState({
    required this.level,
    required this.step,
    required this.steps,
    required this.fixed,
  });

  /// 0 to 1.
  double level;

  /// The current step, 0 to [steps].
  int step;

  /// How many steps the platform has (Android: STREAM_MUSIC's maximum less
  /// its minimum; iOS: 16, as the volume buttons).
  int steps;

  /// The volume cannot be changed (Android isVolumeFixed).
  bool fixed;
}

@HostApi()
abstract class DeviceApi {
  /// Gets the haptic generators ready (iOS); a no-op on Android.
  void prepareHaptics();

  /// Plays [event] with the platform API that decision 0007 names.
  void playHaptic(HapticEvent event);

  /// The media volume now.
  VolumeState getVolume();

  /// Moves the system media volume one platform step up (direction > 0) or
  /// down (< 0) without the system volume panel; returns the new volume.
  VolumeState stepVolume(int direction);
}
