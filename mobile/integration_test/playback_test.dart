// SPDX-License-Identifier: MPL-2.0

// The audio engine on a real device, emulator or simulator: FLAC and MP3 from
// file URIs, a two-item queue, pause, resume and seek, and the media session
// (lock screen and notification) populated. Phase 1 of docs/app/PROGRESS.md.

import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/audio/test_media.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod_audio/neupod_audio.dart';

const _timeout = Duration(seconds: 30);

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // Under `xcodebuild test` (ios/RunnerTests) the tests start as the app
  // launches, and iOS turns accessibility on moments later; wait for it so
  // the binding's SemanticsHandle does not open inside a test (as in
  // app_launch_test.dart).
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });
  // One engine per app, as in the app (playerProvider).
  final audio = NeupodAudio();
  final player = NeupodPlayer(audio);
  const media = TestMedia();

  Future<PlayerState> until(
    bool Function(PlayerState s) test, {
    String? reason,
  }) => player.states
      .firstWhere(test)
      .timeout(
        _timeout,
        onTimeout: () => throw TimeoutException(
          '${reason ?? 'state'} not reached; last: ${player.state}',
        ),
      );

  setUp(() => player.setQueue(const []));

  testWidgets('plays a FLAC to the end', (tester) async {
    await player.setQueue([await media.flacItem()]);
    await player.play();

    final playing = await until((s) => s.isPlaying, reason: 'playing');
    expect(playing.error, isNull);
    expect(playing.format?.sampleRate, 44100);
    expect(playing.format?.channelCount, 2);
    expect(playing.format?.codec, contains('flac'));

    final ended = await until((s) => s.status == PlaybackStatus.ended);
    expect(ended.error, isNull);
    expect(ended.duration?.inMilliseconds, closeTo(300, 30));
  });

  testWidgets('plays an MP3 to the end', (tester) async {
    await player.setQueue([await media.mp3Item()]);
    await player.play();

    final playing = await until((s) => s.isPlaying, reason: 'playing');
    expect(playing.format?.sampleRate, 44100);
    expect(playing.format?.codec, anyOf(contains('mpeg'), contains('mp3')));
    await until((s) => s.status == PlaybackStatus.ended);
    expect(player.state.error, isNull);
  });

  testWidgets('a two-item queue plays FLAC then MP3 and ends', (tester) async {
    final indexes = <int>[];
    final sub = player.states.listen((s) {
      final i = s.index;
      if (i != null && (indexes.isEmpty || indexes.last != i)) indexes.add(i);
    });
    await player.setQueue([await media.flacItem(), await media.mp3Item()]);
    await player.play();

    await until((s) => s.index == 1 && s.isPlaying, reason: 'second item');
    final ended = await until((s) => s.status == PlaybackStatus.ended);
    await sub.cancel();

    expect(ended.index, 1);
    expect(ended.error, isNull);
    expect(indexes, [0, 1]);
  });

  testWidgets('seek while paused, play, pause and resume', (tester) async {
    await player.setQueue([await media.mp3Item()]);
    await until((s) => s.status == PlaybackStatus.ready, reason: 'ready');

    await player.seek(const Duration(milliseconds: 400));
    await until(
      (s) => s.position.inMilliseconds >= 350,
      reason: 'seeked position',
    );
    expect(player.state.isPlaying, isFalse);

    await player.play();
    await until((s) => s.isPlaying, reason: 'playing');
    await player.pause();
    final paused = await until((s) => !s.playWhenReady, reason: 'paused');
    expect(paused.position.inMilliseconds, greaterThanOrEqualTo(350));
    // Paused means the position holds still.
    await Future<void>.delayed(const Duration(milliseconds: 300));
    expect(
      player.currentPosition.inMilliseconds,
      closeTo(paused.position.inMilliseconds, 60),
    );

    await player.play();
    await until((s) => s.status == PlaybackStatus.ended, reason: 'ended');
  });

  testWidgets('the media session shows the item, its art and the controls', (
    tester,
  ) async {
    await player.setQueue([await media.flacItem(), await media.mp3Item()]);
    await until((s) => s.status == PlaybackStatus.ready, reason: 'ready');

    // The session updates asynchronously from the player; poll briefly.
    var session = await player.sessionSnapshot();
    for (
      var i = 0;
      i < 50 && (session.title == null || !session.hasArtwork);
      i++
    ) {
      await Future<void>.delayed(const Duration(milliseconds: 100));
      session = await player.sessionSnapshot();
    }
    debugPrint('media session: $session');
    expect(session.title, 'Test tone (FLAC)');
    expect(session.artist, 'neuPod test tone');
    expect(session.album, 'Fixtures');
    expect(session.hasArtwork, isTrue);
    expect(session.duration?.inMilliseconds, closeTo(300, 30));
    expect(session.commands, containsAll(['play', 'pause', 'next', 'seek']));
    if (defaultTargetPlatform == TargetPlatform.iOS) {
      expect(session.playbackRate, 0);
    }
  });

  testWidgets('the queue id survives the trip through the engine', (
    tester,
  ) async {
    // On Android the id rides in each item's metadata through the
    // MediaController, the session and back in the controller's timeline.
    final mp3 = await media.mp3Item();
    final flac = await media.flacItem();
    await audio.setQueue('itest-queue', [
      EngineItem(id: 'itest:0', uri: mp3.uri, title: mp3.title),
    ]);
    expect((await audio.getState()).queueId, 'itest-queue');

    // Inserted items join the current queue.
    await audio.insert(1, [
      EngineItem(id: 'itest:1', uri: flac.uri, title: flac.title),
    ]);
    await audio.skipTo(1);
    var state = await audio.getState();
    for (var i = 0; i < 50 && state.index != 1; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 100));
      state = await audio.getState();
    }
    expect(state.index, 1);
    expect(state.itemId, 'itest:1');
    expect(state.queueId, 'itest-queue');

    // An emptied queue keeps the id it was given.
    await audio.setQueue('itest-empty', []);
    state = await audio.getState();
    expect(state.queueLength, 0);
    expect(state.queueId, 'itest-empty');
  });

  testWidgets('a file that is not there is reported, not fatal', (
    tester,
  ) async {
    final mp3 = await media.mp3Item();
    final missing = QueueItem(
      id: 'missing',
      uri: '${mp3.uri}.missing',
      title: 'Not there',
    );
    await player.setQueue([missing, mp3]);
    await player.play();

    // The bad entry reports an error and the queue goes on to the MP3.
    await until((s) => s.index == 1 && s.isPlaying, reason: 'skipped to next');
    await until((s) => s.status == PlaybackStatus.ended, reason: 'ended');
  });
}
