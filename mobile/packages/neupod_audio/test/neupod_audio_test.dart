// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod_audio/neupod_audio.dart';
import 'package:neupod_audio/src/messages.g.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  /// Answers every host call on [channel] as the native side would, and
  /// records the arguments.
  List<Object?> recordHostCalls(String method, [Object? reply]) {
    final received = <Object?>[];
    messenger.setMockMessageHandler(
      'dev.flutter.pigeon.neupod_audio.AudioHostApi.$method',
      (message) async {
        received.add(AudioHostApi.pigeonChannelCodec.decodeMessage(message));
        return AudioHostApi.pigeonChannelCodec.encodeMessage([reply]);
      },
    );
    return received;
  }

  test('commands reach the host API with their arguments', () async {
    final audio = NeupodAudio();
    final setQueue = recordHostCalls('setQueue');
    final seek = recordHostCalls('seekTo');
    final skip = recordHostCalls('skipTo');

    await audio.setQueue(
      [EngineItem(id: '1:0', uri: 'content://a', title: 'A')],
      startIndex: 0,
      startPositionMs: 500,
    );
    await audio.seekTo(1200);
    await audio.skipTo(1);

    final args = setQueue.single! as List<Object?>;
    final items = (args[0]! as List<Object?>).cast<EngineItem>();
    expect(items.single.uri, 'content://a');
    expect(items.single.title, 'A');
    expect(args.sublist(1), [0, 500]);
    expect(seek.single, [1200]);
    expect(skip.single, [1]);
    await audio.dispose();
  });

  test('getState returns the engine snapshot', () async {
    final audio = NeupodAudio();
    recordHostCalls(
      'getState',
      EngineEvent(
        status: EngineStatus.ready,
        playWhenReady: true,
        positionMs: 42,
        queueLength: 1,
        index: 0,
        itemId: '1:0',
      ),
    );
    final state = await audio.getState();
    expect(state.status, EngineStatus.ready);
    expect(state.positionMs, 42);
    expect(state.itemId, '1:0');
    await audio.dispose();
  });

  test('native events arrive on the events stream, in order', () async {
    final audio = NeupodAudio();
    final received = <EngineEvent>[];
    final sub = audio.events.listen(received.add);

    Future<void> send(int positionMs) => messenger.handlePlatformMessage(
      'dev.flutter.pigeon.neupod_audio.AudioFlutterApi.onEvent',
      AudioHostApi.pigeonChannelCodec.encodeMessage([
        EngineEvent(
          status: EngineStatus.ready,
          playWhenReady: true,
          positionMs: positionMs,
          queueLength: 1,
        ),
      ]),
      (_) {},
    );
    await send(1);
    await send(2);
    await pumpEventQueue();

    expect(received.map((e) => e.positionMs), [1, 2]);
    await sub.cancel();
    await audio.dispose();
  });

  test('a host error surfaces as a PlatformException', () async {
    final audio = NeupodAudio();
    messenger.setMockMessageHandler(
      'dev.flutter.pigeon.neupod_audio.AudioHostApi.play',
      (_) async => AudioHostApi.pigeonChannelCodec.encodeMessage([
        'detached',
        'The audio engine is not attached',
        null,
      ]),
    );
    await expectLater(audio.play(), throwsA(isA<PlatformException>()));
    await audio.dispose();
  });
}
