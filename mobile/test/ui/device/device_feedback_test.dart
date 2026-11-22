// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/feedback_ports.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/device_options.dart';
import 'package:neupod/ui/device/wheel_bus.dart';

import 'device_test_harness.dart';

class _Clicks implements ClickSoundPort {
  final List<FeedbackKind> played = [];

  @override
  void play(FeedbackKind kind) => played.add(kind);
}

void main() {
  late RecordingHaptics haptics;
  late _Clicks clicks;
  late int nowMs;
  late ProviderContainer c;

  setUp(() {
    haptics = RecordingHaptics();
    clicks = _Clicks();
    nowMs = 0;
    c = ProviderContainer.test(
      overrides: [
        hapticsPortProvider.overrideWithValue(haptics),
        clickSoundPortProvider.overrideWithValue(clicks),
        wheelClockProvider.overrideWithValue(() => nowMs),
      ],
    );
  });

  test('detents: at most 30 a second, the rest dropped', () {
    final f = c.read(deviceFeedbackProvider);
    for (nowMs = 0; nowMs < 1000; nowMs++) {
      f.detent(); // 1,000 a second asked for
    }
    expect(haptics.played, hasLength(30));
  });

  test('presses and holds are never dropped', () {
    final f = c.read(deviceFeedbackProvider);
    f.buttonDown();
    f.buttonUp();
    f.holdTookEffect();
    f.endReached();
    expect(haptics.played, [
      FeedbackKind.buttonDown,
      FeedbackKind.buttonUp,
      FeedbackKind.holdTookEffect,
      FeedbackKind.endReached,
    ]);
  });

  test('the in-app Haptics setting turns them all off', () {
    c
        .read(deviceViewOptionsProvider.notifier)
        .update((o) => o.copyWith(haptics: false));
    final f = c.read(deviceFeedbackProvider);
    f.prepare();
    f.detent();
    f.buttonDown();
    expect(haptics.played, isEmpty);
    expect(haptics.prepared, 0);
  });

  test(
    'the click sound plays only when its setting is on (Off by default)',
    () {
      final f = c.read(deviceFeedbackProvider);
      f.buttonDown();
      expect(clicks.played, isEmpty);
      c
          .read(deviceViewOptionsProvider.notifier)
          .update((o) => o.copyWith(clickSound: true));
      nowMs = 100;
      f.detent();
      f.buttonDown();
      f.buttonUp(); // one click per press, on the way down
      expect(clicks.played, [FeedbackKind.detent, FeedbackKind.buttonDown]);
    },
  );

  test('the in-memory system volume steps one at a time and clamps', () async {
    final v = MemorySystemVolume(current: 15, steps: 16);
    expect((await v.step(5)).step, 16);
    expect((await v.step(1)).step, 16);
    expect((await v.step(-3)).step, 15);
    expect((await v.read()).percent, 94);
  });

  test(
    'the click sound is the platform system click (follows the ringer)',
    () async {
      TestWidgetsFlutterBinding.ensureInitialized();
      final calls = <MethodCall>[];
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(SystemChannels.platform, (call) async {
            calls.add(call);
            return null;
          });
      addTearDown(
        () => TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
            .setMockMethodCallHandler(SystemChannels.platform, null),
      );
      const SystemClicks().play(FeedbackKind.detent);
      await Future<void>.delayed(Duration.zero);
      expect(calls.single.method, 'SystemSound.play');
      expect(calls.single.arguments, 'SystemSoundType.click');
      expect(
        ProviderContainer.test().read(clickSoundPortProvider),
        isA<SystemClicks>(),
      );
    },
  );
}
