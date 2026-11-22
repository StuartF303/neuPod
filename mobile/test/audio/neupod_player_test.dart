// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod_audio/neupod_audio.dart';

/// Stands in for the native engine: records commands, and lets a test send
/// the events the engine would.
class FakeNeupodAudio implements NeupodAudio {
  final _events = StreamController<EngineEvent>.broadcast(sync: true);
  final calls = <String>[];
  List<EngineItem> queue = [];
  EngineEvent initial = EngineEvent(
    status: EngineStatus.idle,
    playWhenReady: false,
    positionMs: 0,
    queueLength: 0,
  );
  SessionInfo session = SessionInfo(hasArtwork: false);

  void emit(EngineEvent event) => _events.add(event);

  @override
  Stream<EngineEvent> get events => _events.stream;

  @override
  Future<void> setQueue(
    List<EngineItem> items, {
    int startIndex = 0,
    int startPositionMs = 0,
  }) async {
    queue = items;
    calls.add('setQueue($startIndex, $startPositionMs)');
  }

  @override
  Future<void> insert(int index, List<EngineItem> items) async =>
      calls.add('insert($index)');

  @override
  Future<void> remove(int index) async => calls.add('remove($index)');

  @override
  Future<void> move(int from, int to) async => calls.add('move($from, $to)');

  @override
  Future<void> clear() async => calls.add('clear');

  @override
  Future<void> play() async => calls.add('play');

  @override
  Future<void> pause() async => calls.add('pause');

  @override
  Future<void> seekTo(int positionMs) async => calls.add('seekTo($positionMs)');

  @override
  Future<void> skipTo(int index) async => calls.add('skipTo($index)');

  @override
  Future<void> skipToNext() async => calls.add('skipToNext');

  @override
  Future<void> skipToPrevious() async => calls.add('skipToPrevious');

  @override
  Future<EngineEvent> getState() async => initial;

  @override
  Future<SessionInfo> getSessionInfo() async => session;

  @override
  Future<void> dispose() => _events.close();
}

const _flac = QueueItem(
  id: 't1',
  uri: 'content://x/1.flac',
  title: 'One',
  artist: 'A',
  album: 'B',
  artUri: 'file:///art.png',
  duration: Duration(seconds: 3),
);
const _mp3 = QueueItem(id: 't2', uri: 'content://x/2.mp3', title: 'Two');

EngineEvent _event({
  required String? itemId,
  int? index = 0,
  EngineStatus status = EngineStatus.ready,
  bool playWhenReady = true,
  int positionMs = 0,
  int? durationMs = 3000,
  int queueLength = 2,
  String? error,
  EngineFormat? format,
}) => EngineEvent(
  status: status,
  playWhenReady: playWhenReady,
  index: index,
  itemId: itemId,
  positionMs: positionMs,
  durationMs: durationMs,
  queueLength: queueLength,
  errorMessage: error,
  format: format,
);

void main() {
  late FakeNeupodAudio audio;
  late Duration now;
  late NeupodPlayer player;

  setUp(() async {
    audio = FakeNeupodAudio();
    now = Duration.zero;
    player = NeupodPlayer(audio, clock: () => now);
    await pumpEventQueue();
  });

  tearDown(() => player.dispose());

  test('setQueue sends every field, with an id unique to this queue', () async {
    await player.setQueue([_flac, _mp3], startIndex: 1);

    expect(audio.calls, ['setQueue(1, 0)']);
    expect(audio.queue.map((i) => i.id), ['1:0', '1:1']);
    final first = audio.queue.first;
    expect(first.uri, _flac.uri);
    expect(first.title, 'One');
    expect(first.artist, 'A');
    expect(first.album, 'B');
    expect(first.artUri, 'file:///art.png');
    expect(first.durationMs, 3000);
    expect(player.state.queue, [_flac, _mp3]);
    expect(player.state.index, 1);
    expect(player.state.status, PlaybackStatus.loading);

    await player.setQueue([_flac]);
    expect(audio.queue.single.id, '2:0');
  });

  test('setQueue rejects a start index outside the queue', () {
    expect(() => player.setQueue([_flac], startIndex: 1), throwsRangeError);
  });

  test('engine events become player states', () async {
    await player.setQueue([_flac, _mp3]);
    final states = <PlayerState>[];
    final sub = player.states.listen(states.add);
    await pumpEventQueue();

    audio.emit(
      _event(
        itemId: '1:1',
        index: 1,
        positionMs: 250,
        format: EngineFormat(
          codec: 'audio/mpeg',
          sampleRate: 44100,
          channelCount: 2,
        ),
      ),
    );

    final s = player.state;
    expect(s.status, PlaybackStatus.ready);
    expect(s.playWhenReady, isTrue);
    expect(s.isPlaying, isTrue);
    expect(s.index, 1);
    expect(s.current, _mp3);
    expect(s.position, const Duration(milliseconds: 250));
    expect(s.duration, const Duration(seconds: 3));
    expect(
      s.format,
      const StreamFormat(
        codec: 'audio/mpeg',
        sampleRate: 44100,
        channelCount: 2,
      ),
    );
    await pumpEventQueue();
    expect(states.last, s);
    await sub.cancel();
  });

  test('states starts with the current snapshot', () async {
    await player.setQueue([_flac]);
    expect(await player.states.first, player.state);
  });

  test('events about an earlier queue do not move the current one', () async {
    await player.setQueue([_flac, _mp3]);
    await player.setQueue([_mp3]);

    audio.emit(_event(itemId: '1:1', index: 1, positionMs: 999));

    expect(player.state.index, 0);
    expect(player.state.queue, [_mp3]);
    expect(player.state.position, Duration.zero);
  });

  test(
    'an engine already playing at start-up shows its transport state',
    () async {
      final fresh = FakeNeupodAudio()
        ..initial = _event(itemId: 'old:0', queueLength: 1);
      final p = NeupodPlayer(fresh);
      await pumpEventQueue();

      expect(p.state.status, PlaybackStatus.ready);
      expect(p.state.playWhenReady, isTrue);
      expect(p.state.queue, isEmpty);
      await p.dispose();
    },
  );

  test('an emptied engine queue empties the player', () async {
    await player.setQueue([_flac]);
    audio.emit(
      _event(
        itemId: null,
        index: null,
        status: EngineStatus.idle,
        queueLength: 0,
      ),
    );
    expect(player.state.queue, isEmpty);
    expect(player.state.index, isNull);
  });

  test('the position runs on while playing and stops at the end', () async {
    await player.setQueue([_flac, _mp3]);
    audio.emit(_event(itemId: '1:0', positionMs: 1000));

    now = const Duration(milliseconds: 500);
    expect(player.currentPosition, const Duration(milliseconds: 1500));
    now = const Duration(seconds: 10);
    expect(player.currentPosition, const Duration(seconds: 3));

    audio.emit(_event(itemId: '1:0', positionMs: 1200, playWhenReady: false));
    now = const Duration(seconds: 20);
    expect(player.currentPosition, const Duration(milliseconds: 1200));
  });

  test('positions yields the extrapolated position', () async {
    await player.setQueue([_flac]);
    audio.emit(_event(itemId: '1:0', positionMs: 400, queueLength: 1));
    expect(await player.positions().first, const Duration(milliseconds: 400));
  });

  test('an unplayable item is skipped, once, and playback goes on', () async {
    await player.setQueue([_flac, _mp3]);
    audio.emit(
      _event(itemId: '1:0', status: EngineStatus.idle, error: 'cannot decode'),
    );
    await pumpEventQueue();

    expect(player.state.error, 'cannot decode');
    expect(audio.calls.skip(1), ['skipTo(1)', 'play']);

    audio.emit(
      _event(itemId: '1:0', status: EngineStatus.idle, error: 'cannot decode'),
    );
    await pumpEventQueue();
    expect(audio.calls.skip(1), ['skipTo(1)', 'play']);
  });

  test('an unplayable last item stops without skipping', () async {
    await player.setQueue([_flac, _mp3]);
    audio.emit(
      _event(
        itemId: '1:1',
        index: 1,
        status: EngineStatus.idle,
        error: 'cannot decode',
      ),
    );
    await pumpEventQueue();
    expect(audio.calls, ['setQueue(0, 0)']);
  });

  test('transport commands reach the engine', () async {
    await player.play();
    await player.pause();
    await player.seek(const Duration(milliseconds: 1500));
    await player.skipTo(1);
    await player.skipToNext();
    await player.skipToPrevious();
    expect(audio.calls, [
      'play',
      'pause',
      'seekTo(1500)',
      'skipTo(1)',
      'skipToNext',
      'skipToPrevious',
    ]);
  });

  test('sessionSnapshot maps the media session', () async {
    audio.session = SessionInfo(
      title: 'One',
      artist: 'A',
      album: 'B',
      durationMs: 3000,
      positionMs: 1000,
      playbackRate: 1,
      hasArtwork: true,
      commands: ['play', 'pause', 'seek'],
    );
    final s = await player.sessionSnapshot();
    expect(s.title, 'One');
    expect(s.artist, 'A');
    expect(s.album, 'B');
    expect(s.duration, const Duration(seconds: 3));
    expect(s.position, const Duration(seconds: 1));
    expect(s.playbackRate, 1);
    expect(s.hasArtwork, isTrue);
    expect(s.commands, {'play', 'pause', 'seek'});
  });
}
