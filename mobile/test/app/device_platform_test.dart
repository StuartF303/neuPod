// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/device_platform.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod_device/neupod_device.dart';
import 'package:neupod_device/src/device_api.g.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('every feedback kind has its own platform event', () {
    final events = FeedbackKind.values.map(PlatformHaptics.eventFor).toSet();
    expect(events, hasLength(FeedbackKind.values.length));
    expect(events, HapticEvent.values.toSet());
    expect(
      PlatformHaptics.eventFor(FeedbackKind.endReached),
      HapticEvent.endReached,
    );
  });

  test('main installs the haptics, volume and accessibility overrides', () {
    expect(devicePlatformOverrides(), hasLength(3));
  });

  test('a volume step that did not land reaches the app as failed', () async {
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    const codec = DeviceApi.pigeonChannelCodec;
    const channel = 'dev.flutter.pigeon.neupod_device.DeviceApi.stepVolume';
    for (final (platform, app) in [
      (VolumeStatus.ok, SystemVolumeStatus.ok),
      (VolumeStatus.noSlider, SystemVolumeStatus.noSlider),
      (VolumeStatus.noEffect, SystemVolumeStatus.noEffect),
      (VolumeStatus.fixed, SystemVolumeStatus.fixed),
    ]) {
      messenger.setMockMessageHandler(channel, (data) async {
        return codec.encodeMessage(<Object?>[
          VolumeState(
            level: 0.5,
            step: 8,
            steps: 16,
            fixed: platform == VolumeStatus.fixed,
            status: platform,
          ),
        ]);
      });
      final v = await PlatformSystemVolume(NeupodDevice()).step(1);
      expect(v.status, app);
      expect(v.failed, app != SystemVolumeStatus.ok);
      expect(v.step, 8); // the level read back, not the target
    }
    messenger.setMockMessageHandler(channel, null);
  });
}
