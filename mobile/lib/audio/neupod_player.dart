// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/playback/player.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod_audio/neupod_audio.dart';

/// The [Player] port on the in-repo engine plugin `neupod_audio` (decisions
/// 0001 and 0002).
///
/// The native queue mirrors [PlayerState.queue] index for index. Each queue
/// entry gets an engine id unique to the [setQueue] call that made it, so an
/// event about an earlier queue is never applied to the current one.
class NeupodPlayer implements Player {
  NeupodPlayer(this._audio, {Duration Function()? clock})
    : _clock = clock ?? _monotonic {
    _subscription = _audio.events.listen(_onEvent);
    unawaited(_audio.getState().then(_onEvent, onError: (Object _) {}));
  }

  final NeupodAudio _audio;
  final Duration Function() _clock;
  late final StreamSubscription<EngineEvent> _subscription;
  final _states = StreamController<PlayerState>.broadcast();

  PlayerState _state = PlayerState.empty;
  int _generation = 0;

  /// When [_state] was taken, on [_clock]; the base of [currentPosition].
  Duration _stateTakenAt = Duration.zero;

  /// The engine id of the entry an error was last handled for.
  String? _skippedAfterError;

  @override
  PlayerState get state => _state;

  @override
  Stream<PlayerState> get states async* {
    yield _state;
    yield* _states.stream;
  }

  /// The position now: the last reported one, plus the time since while
  /// playing, never past the end.
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
    _generation++;
    _skippedAfterError = null;
    final prefix = '$_generation:';
    _publish(
      _state.copyWith(
        queue: List.unmodifiable(items),
        index: () => items.isEmpty ? null : startIndex,
        position: startPosition,
        status: items.isEmpty ? PlaybackStatus.idle : PlaybackStatus.loading,
        duration: () => items.isEmpty ? null : items[startIndex].duration,
        format: () => null,
        error: () => null,
      ),
    );
    await _audio.setQueue(
      [
        for (final (i, item) in items.indexed)
          EngineItem(
            id: '$prefix$i',
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

  @override
  Future<void> dispose() async {
    await _subscription.cancel();
    await _states.close();
  }

  void _onEvent(EngineEvent event) {
    if (_states.isClosed) return;
    final ours = event.itemId?.startsWith('$_generation:') ?? false;
    final emptied = event.queueLength == 0;
    if (!ours && !emptied) {
      // About an earlier queue (or one set before this app started, which
      // resuming will adopt in phase 4): keep the transport state only.
      if (_state.queue.isEmpty) {
        _publish(
          _state.copyWith(
            status: _status(event.status),
            playWhenReady: event.playWhenReady,
          ),
        );
      }
      return;
    }
    final format = event.format;
    _publish(
      _state.copyWith(
        status: _status(event.status),
        playWhenReady: event.playWhenReady,
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
  /// next entry, once per failed entry.
  void _skipIfUnplayable(EngineEvent event) {
    final index = event.index;
    if (event.errorMessage == null || index == null) return;
    if (_skippedAfterError == event.itemId) return;
    _skippedAfterError = event.itemId;
    if (index + 1 >= event.queueLength) return;
    unawaited(() async {
      await _audio.skipTo(index + 1);
      if (event.playWhenReady) await _audio.play();
    }());
  }

  void _publish(PlayerState next) {
    _stateTakenAt = _clock();
    if (next == _state) return;
    _state = next;
    _states.add(next);
  }

  static PlaybackStatus _status(EngineStatus status) => switch (status) {
    EngineStatus.idle => PlaybackStatus.idle,
    EngineStatus.loading => PlaybackStatus.loading,
    EngineStatus.ready => PlaybackStatus.ready,
    EngineStatus.ended => PlaybackStatus.ended,
  };
}

final _stopwatch = Stopwatch()..start();
Duration _monotonic() => _stopwatch.elapsed;
