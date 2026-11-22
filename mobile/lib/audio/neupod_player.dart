// SPDX-License-Identifier: MPL-2.0

import 'dart:async';
import 'dart:math';

import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod_audio/neupod_audio.dart';

/// The [Player] port on the in-repo engine plugin `neupod_audio` (decisions
/// 0001 and 0002).
///
/// The native queue mirrors [PlayerState.queue] index for index. Every
/// [setQueue] gets a fresh random queue id, and only engine events carrying
/// that id are applied. So an event about an earlier queue is never applied
/// to the current one: not one still in flight when the queue changed, not
/// an empty-queue snapshot answering an earlier request, and not one about a
/// queue an earlier Flutter engine set while the Android service lived on.
class NeupodPlayer implements Player {
  NeupodPlayer(
    this._audio, {
    Duration Function()? clock,
    String Function()? newQueueId,
  }) : _clock = clock ?? _monotonic,
       _newQueueId = newQueueId ?? _randomQueueId {
    _subscription = _audio.events.listen(_onEvent);
    unawaited(_audio.getState().then(_onEvent, onError: (Object _) {}));
  }

  final NeupodAudio _audio;
  final Duration Function() _clock;
  final String Function() _newQueueId;
  late final StreamSubscription<EngineEvent> _subscription;
  final _states = StreamController<PlayerState>.broadcast();

  PlayerState _state = PlayerState.empty;

  /// The id of the queue this player last set; null until it sets one.
  String? _queueId;

  /// When [_state] was taken, on [_clock]; the base of [currentPosition].
  Duration _stateTakenAt = Duration.zero;

  /// The entry (queue id and item id) an error was last handled for.
  String? _skippedAfterError;

  @override
  PlayerState get state => _state;

  @override
  Stream<PlayerState> get states async* {
    yield _state;
    yield* _states.stream;
  }

  /// The position now: the last reported one, plus the time since while
  /// sound is playing, never past the end.
  Duration get currentPosition {
    if (!_state.isPlaying) return _state.position;
    final position = _state.position + (_clock() - _stateTakenAt);
    final duration = _state.duration;
    return duration != null && position > duration ? duration : position;
  }

  @override
  Stream<Duration> positions({
    Duration interval = const Duration(milliseconds: 200),
  }) async* {
    yield currentPosition;
    await for (final _ in Stream<void>.periodic(interval)) {
      yield currentPosition;
    }
  }

  @override
  Future<void> setQueue(
    List<QueueItem> items, {
    int startIndex = 0,
    Duration startPosition = Duration.zero,
  }) async {
    if (items.isNotEmpty) RangeError.checkValidIndex(startIndex, items);
    final queueId = _newQueueId();
    _queueId = queueId;
    _skippedAfterError = null;
    _publish(
      _state.copyWith(
        queue: List.unmodifiable(items),
        index: () => items.isEmpty ? null : startIndex,
        position: startPosition,
        status: items.isEmpty ? PlaybackStatus.idle : PlaybackStatus.loading,
        suppressed: false,
        duration: () => items.isEmpty ? null : items[startIndex].duration,
        format: () => null,
        error: () => null,
      ),
    );
    await _audio.setQueue(
      queueId,
      [
        for (final (i, item) in items.indexed)
          EngineItem(
            id: '$queueId:$i',
            uri: item.uri,
            title: item.title,
            artist: item.artist,
            album: item.album,
            artUri: item.artUri,
            durationMs: item.duration?.inMilliseconds,
          ),
      ],
      startIndex: startIndex,
      startPositionMs: startPosition.inMilliseconds,
    );
  }

  @override
  Future<void> play() => _audio.play();

  @override
  Future<void> pause() => _audio.pause();

  @override
  Future<void> seek(Duration position) =>
      _audio.seekTo(position.inMilliseconds);

  @override
  Future<void> skipTo(int index) => _audio.skipTo(index);

  @override
  Future<void> skipToNext() => _audio.skipToNext();

  @override
  Future<void> skipToPrevious() => _audio.skipToPrevious();

  @override
  Future<MediaSessionSnapshot> sessionSnapshot() async {
    final info = await _audio.getSessionInfo();
    Duration? ms(int? v) => v == null ? null : Duration(milliseconds: v);
    return MediaSessionSnapshot(
      title: info.title,
      artist: info.artist,
      album: info.album,
      duration: ms(info.durationMs),
      position: ms(info.positionMs),
      playbackRate: info.playbackRate,
      hasArtwork: info.hasArtwork,
      commands: {...?info.commands},
    );
  }

  /// Stops listening and unregisters the event handler. The native engine
  /// keeps playing (on Android it belongs to the media session service).
  @override
  Future<void> dispose() async {
    await _subscription.cancel();
    await _states.close();
    await _audio.dispose();
  }

  void _onEvent(EngineEvent event) {
    if (_states.isClosed) return;
    if (event.queueId != _queueId) {
      // About another queue. Before this player has set one, show what an
      // engine that is already playing is doing (resume adopts its queue in
      // phase 4); after that, ignore it.
      if (_queueId == null && event.queueId != null) {
        _publish(
          _state.copyWith(
            status: _status(event.status),
            playWhenReady: event.playWhenReady,
            suppressed: _suppressed(event),
          ),
        );
      }
      return;
    }
    final emptied = event.queueLength == 0;
    final format = event.format;
    _publish(
      _state.copyWith(
        status: _status(event.status),
        playWhenReady: event.playWhenReady,
        suppressed: _suppressed(event),
        queue: emptied ? const [] : null,
        index: () => emptied ? null : event.index,
        position: Duration(milliseconds: event.positionMs),
        duration: () => event.durationMs == null
            ? null
            : Duration(milliseconds: event.durationMs!),
        format: () => format == null
            ? null
            : StreamFormat(
                codec: format.codec,
                sampleRate: format.sampleRate,
                channelCount: format.channelCount,
                bitsPerSample: format.bitsPerSample,
              ),
        error: () => event.errorMessage,
      ),
    );
    _skipIfUnplayable(event);
  }

  /// An unplayable file never stops the queue (spec, Formats): go on to the
  /// next entry, once per failed entry, and only if the queue is still the
  /// one that failed when the skip is sent.
  void _skipIfUnplayable(EngineEvent event) {
    final index = event.index;
    if (event.errorMessage == null || index == null) return;
    final entry = '${event.queueId}/${event.itemId}';
    if (_skippedAfterError == entry) return;
    _skippedAfterError = entry;
    if (index + 1 >= event.queueLength) return;
    final queueId = event.queueId;
    // After the current turn, so a queue replaced meanwhile is seen.
    unawaited(
      Future.microtask(() async {
        if (_queueId != queueId) return;
        await _audio.skipTo(index + 1);
        if (event.playWhenReady && _queueId == queueId) await _audio.play();
      }),
    );
  }

  void _publish(PlayerState next) {
    _stateTakenAt = _clock();
    if (next == _state) return;
    _state = next;
    _states.add(next);
  }

  static bool _suppressed(EngineEvent e) =>
      e.playWhenReady && e.status == EngineStatus.ready && !e.isPlaying;

  static PlaybackStatus _status(EngineStatus status) => switch (status) {
    EngineStatus.idle => PlaybackStatus.idle,
    EngineStatus.loading => PlaybackStatus.loading,
    EngineStatus.ready => PlaybackStatus.ready,
    EngineStatus.ended => PlaybackStatus.ended,
  };
}

final _stopwatch = Stopwatch()..start();
Duration _monotonic() => _stopwatch.elapsed;

final _random = Random.secure();

/// 64 random bits as hex: unique across queues and app runs in practice.
String _randomQueueId() => [
  for (var i = 0; i < 4; i++)
    _random.nextInt(1 << 16).toRadixString(16).padLeft(4, '0'),
].join();
