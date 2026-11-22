// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/device_platform.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod_device/neupod_device.dart';

void main() {
  test('every feedback kind has its own platform event', () {
    final events = FeedbackKind.values.map(PlatformHaptics.eventFor).toSet();
    expect(events, hasLength(FeedbackKind.values.length));
    expect(events, HapticEvent.values.toSet());
    expect(
      PlatformHaptics.eventFor(FeedbackKind.endReached),
      HapticEvent.endReached,
    );
  });

  test('main installs the haptics and volume overrides', () {
    expect(devicePlatformOverrides(), hasLength(2));
  });
}
