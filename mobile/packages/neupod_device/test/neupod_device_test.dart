// SPDX-License-Identifier: MPL-2.0

// The Dart side of the plugin against a fake host: the right channel
// messages go out and the replies come back as VolumeState.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod_device/neupod_device.dart';
import 'package:neupod_device/src/device_api.g.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
  const codec = DeviceApi.pigeonChannelCodec;
  const prefix = 'dev.flutter.pigeon.neupod_device.DeviceApi';

  final calls = <String, List<Object?>>{};

  setUp(() {
    calls.clear();
    for (final name in [
      'prepareHaptics',
      'playHaptic',
      'getVolume',
      'stepVolume',
    ]) {
      messenger.setMockMessageHandler('$prefix.$name', (data) async {
        final args = codec.decodeMessage(data) as List<Object?>?;
        calls[name] = args ?? const [];
        final reply = switch (name) {
          'getVolume' => VolumeState(
            level: 0.5,
            step: 8,
            steps: 16,
            fixed: false,
            status: VolumeStatus.ok,
          ),
          'stepVolume' => VolumeState(
            level: (8 + (args![0]! as int)) / 16,
            step: 8 + (args[0]! as int),
            steps: 16,
            fixed: false,
            status: VolumeStatus.ok,
          ),
          _ => null,
        };
        return codec.encodeMessage(<Object?>[reply]);
      });
    }
  });

  test('haptics', () async {
    final d = NeupodDevice();
    await d.prepareHaptics();
    expect(calls.containsKey('prepareHaptics'), isTrue);
    await d.playHaptic(HapticEvent.endReached);
    expect(calls['playHaptic'], [HapticEvent.endReached]);
  });

  test(
    'volume: read and step by one platform step whatever the size',
    () async {
      final d = NeupodDevice();
      final v = await d.volume();
      expect(v.step, 8);
      expect(v.steps, 16);
      final up = await d.stepVolume(3);
      expect(calls['stepVolume'], [1]);
      expect(up.step, 9);
      final down = await d.stepVolume(-2);
      expect(calls['stepVolume'], [-1]);
      expect(down.level, 7 / 16);
    },
  );
}
