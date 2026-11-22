// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod_audio/neupod_audio.dart';

/// Stands in for the native engine: records commands, and lets a test send
/// the events the engine would. [getState] can be held back with
/// [stateReply] to reorder its answer against later events.
class FakeNeupodAudio implements NeupodAudio {
  final _events = StreamController<EngineEvent>.broadcast(sync: true);
  final calls = <String>[];
  List<EngineItem> queue = [];
  String? queueId;
  Completer<EngineEvent>? stateReply;
  EngineEvent initial = _event(
    queueId: null,
    itemId: null,
    index: null,
    status: EngineStatus.idle,
    playWhenReady: false,
    queueLength: 0,
  );
  SessionInfo session = SessionInfo(hasArtwork: false);
  bool disposed = false;

  void emit(EngineEvent event) => _events.add(event);

  @override
  Stream<EngineEvent> get events => _events.stream;

  @override
  Future<void> setQueue(
    String queueId,
    List<EngineItem> items, {
    int startIndex = 0,
    int startPositionMs = 0,
  }) async {
    this.queueId = queueId;
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
  Future<EngineEvent> getState() => stateReply?.future ?? Future.value(initial);

  @override
  Future<SessionInfo> getSessionInfo() async => session;

  @override
  Future<void> dispose() async {
    disposed = true;
    await _events.close();
  }
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
  required String? queueId,
  String? itemId,
  int? index = 0,
  EngineStatus status = EngineStatus.ready,
  bool playWhenReady = true,
  bool? isPlaying,
  int positionMs = 0,
  int? durationMs = 3000,
  int queueLength = 2,
  String? error,
  EngineFormat? format,
}) => EngineEvent(
  queueId: queueId,
  status: status,
  playWhenReady: playWhenReady,
  isPlaying: isPlaying ?? (playWhenReady && status == EngineStatus.ready),
  index: index,
  itemId:
      itemId ?? (queueId == null || index == null ? null : '$queueId:$index'),
  positionMs: positionMs,
  durationMs: durationMs,
  queueLength: queueLength,
  errorMessage: error,
  format: format,
);

/// An empty, idle engine with no queue id: a fresh engine's snapshot.
EngineEvent _emptyEngine() => _event(
  queueId: null,
  index: null,
  status: EngineStatus.idle,
  playWhenReady: false,
  queueLength: 0,
);

void main() {
  late FakeNeupodAudio audio;
  late Duration now;
  late NeupodPlayer player;
  late int queues;

  NeupodPlayer makePlayer(FakeNeupodAudio audio) =>
      NeupodPlayer(audio, clock: () => now, newQueueId: () => 'q${++queues}');

  setUp(() async {
    audio = FakeNeupodAudio();
    now = Duration.zero;
    queues = 0;
    player = makePlayer(audio);
    await pumpEventQueue();
  });

  tearDown(() => player.dispose());

  test('setQueue sends every field, under a new queue id', () async {
    await player.setQueue([_flac, _mp3], startIndex: 1);

    expect(audio.calls, ['setQueue(1, 0)']);
    expect(audio.queueId, 'q1');
    expect(audio.queue.map((i) => i.id), ['q1:0', 'q1:1']);
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
    expect(audio.queueId, 'q2');
  });

  test('queue ids are random by default', () async {
    final a = FakeNeupodAudio();
    final p = NeupodPlayer(a);
    await p.setQueue([_flac]);
    final first = a.queueId;
    await p.setQueue([_flac]);
    expect(first, matches(RegExp(r'^[0-9a-f]{16}$')));
    expect(a.queueId, isNot(first));
    await p.dispose();
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
        queueId: 'q1',
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

    audio.emit(_event(queueId: 'q1', index: 1, positionMs: 999));

    expect(player.state.index, 0);
    expect(player.state.queue, [_mp3]);
    expect(player.state.position, Duration.zero);
  });

  test('a late empty getState reply does not wipe a queue set since', () async {
    final a = FakeNeupodAudio()..stateReply = Completer<EngineEvent>();
    final p = makePlayer(a);
    await p.setQueue([_flac, _mp3]);
    a.emit(_event(queueId: 'q1', positionMs: 100));

    a.stateReply!.complete(_emptyEngine());
    await pumpEventQueue();

    expect(p.state.queue, [_flac, _mp3]);
    expect(p.state.index, 0);
    expect(p.state.position, const Duration(milliseconds: 100));
    await p.dispose();
  });

  test(
    'an empty snapshot before setQueue is applied, then the queue',
    () async {
      final a = FakeNeupodAudio()..stateReply = Completer<EngineEvent>();
      final p = makePlayer(a);
      a.stateReply!.complete(_emptyEngine());
      await pumpEventQueue();
      expect(p.state.queue, isEmpty);

      await p.setQueue([_flac]);
      a.emit(_event(queueId: 'q1', queueLength: 1));
      expect(p.state.queue, [_flac]);
      expect(p.state.status, PlaybackStatus.ready);
      await p.dispose();
    },
  );

  test(
    'an Android controller-connected empty event after setQueue is ignored',
    () async {
      await player.setQueue([_flac]);
      audio.emit(_emptyEngine());
      expect(player.state.queue, [_flac]);
      expect(player.state.index, 0);
    },
  );

  test(
    'an old queue from a service that outlived the app is not adopted',
    () async {
      // The service still plays a queue an earlier Flutter engine set. Its
      // item ids even look like this player's would.
      final a = FakeNeupodAudio()
        ..initial = _event(queueId: 'old', itemId: 'q1:0', queueLength: 1);
      final p = makePlayer(a);
      await pumpEventQueue();
      expect(p.state.status, PlaybackStatus.ready);
      expect(p.state.playWhenReady, isTrue);
      expect(p.state.queue, isEmpty);

      await p.setQueue([_mp3]);
      a.emit(_event(queueId: 'old', itemId: 'q1:0', positionMs: 777));
      expect(p.state.queue, [_mp3]);
      expect(p.state.position, Duration.zero);
      expect(p.state.status, PlaybackStatus.loading);
      await p.dispose();
    },
  );

  test(
    'before any setQueue, a non-empty queue without an id is transport only',
    () async {
      audio.emit(_event(queueId: null, itemId: 'x:0', positionMs: 500));
      expect(player.state.status, PlaybackStatus.ready);
      expect(player.state.playWhenReady, isTrue);
      expect(player.state.queue, isEmpty);
      expect(player.state.index, isNull);
      expect(player.state.position, Duration.zero);
    },
  );

  test('without a duration the position is not extrapolated', () async {
    await player.setQueue([_mp3]);
    audio.emit(
      _event(queueId: 'q1', positionMs: 700, durationMs: null, queueLength: 1),
    );
    now = const Duration(hours: 1);
    expect(player.currentPosition, const Duration(milliseconds: 700));
  });

  test('an emptied queue of our own empties the player', () async {
    await player.setQueue([_flac]);
    audio.emit(
      _event(
        queueId: 'q1',
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
    audio.emit(_event(queueId: 'q1', positionMs: 1000));

    now = const Duration(milliseconds: 500);
    expect(player.currentPosition, const Duration(milliseconds: 1500));
    now = const Duration(seconds: 10);
    expect(player.currentPosition, const Duration(seconds: 3));

    audio.emit(_event(queueId: 'q1', positionMs: 1200, playWhenReady: false));
    now = const Duration(seconds: 20);
    expect(player.currentPosition, const Duration(milliseconds: 1200));
  });

  test('the position holds while playback is suppressed', () async {
    await player.setQueue([_flac]);
    audio.emit(
      _event(queueId: 'q1', positionMs: 1000, isPlaying: false, queueLength: 1),
    );
    expect(player.state.suppressed, isTrue);
    expect(player.state.playWhenReady, isTrue);
    expect(player.state.isPlaying, isFalse);
    now = const Duration(seconds: 1);
    expect(player.currentPosition, const Duration(milliseconds: 1000));
  });

  test('positions yields the extrapolated position', () async {
    await player.setQueue([_flac]);
    audio.emit(_event(queueId: 'q1', positionMs: 400, queueLength: 1));
    expect(await player.positions().first, const Duration(milliseconds: 400));
  });

  test('an unplayable item is skipped, once, and playback goes on', () async {
    await player.setQueue([_flac, _mp3]);
    audio.emit(
      _event(queueId: 'q1', status: EngineStatus.idle, error: 'cannot decode'),
    );
    await pumpEventQueue();

    expect(player.state.error, 'cannot decode');
    expect(audio.calls.skip(1), ['skipTo(1)', 'play']);

    audio.emit(
      _event(queueId: 'q1', status: EngineStatus.idle, error: 'cannot decode'),
    );
    await pumpEventQueue();
    expect(audio.calls.skip(1), ['skipTo(1)', 'play']);
  });

  test(
    'a skip after an error is dropped if the queue changed meanwhile',
    () async {
      await player.setQueue([_flac, _mp3]);
      audio.emit(
        _event(
          queueId: 'q1',
          status: EngineStatus.idle,
          error: 'cannot decode',
        ),
      );
      // Same turn: the queue is replaced before the skip runs.
      final replacing = player.setQueue([_mp3]);
      await replacing;
      await pumpEventQueue();
      expect(audio.calls.where((c) => c.startsWith('skipTo')), isEmpty);
    },
  );

  test('an unplayable last item stops without skipping', () async {
    await player.setQueue([_flac, _mp3]);
    audio.emit(
      _event(
        queueId: 'q1',
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

  test('dispose releases the engine bridge', () async {
    final a = FakeNeupodAudio();
    final p = makePlayer(a);
    await p.dispose();
    expect(a.disposed, isTrue);
  });
}
