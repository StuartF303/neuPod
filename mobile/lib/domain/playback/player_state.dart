// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/playback/queue_item.dart';

/// What the engine is doing, independent of whether the user asked to play.
enum PlaybackStatus {
  /// Nothing loaded, or stopped after an error.
  idle,

  /// Opening or buffering the current item.
  loading,

  /// Ready: playing if [PlayerState.playWhenReady], otherwise paused.
  ready,

  /// The last item in the queue has finished.
  ended,
}

/// The decoded stream as the platform reports it; null fields are unknown.
/// Feeds the Now Playing format line (`FLAC 16/44.1`) in phase 5.
final class StreamFormat {
  const StreamFormat({
    this.codec,
    this.sampleRate,
    this.channelCount,
    this.bitsPerSample,
  });

  final String? codec;
  final int? sampleRate;
  final int? channelCount;
  final int? bitsPerSample;

  @override
  bool operator ==(Object other) =>
      other is StreamFormat &&
      other.codec == codec &&
      other.sampleRate == sampleRate &&
      other.channelCount == channelCount &&
      other.bitsPerSample == bitsPerSample;

  @override
  int get hashCode =>
      Object.hash(codec, sampleRate, channelCount, bitsPerSample);

  @override
  String toString() =>
      'StreamFormat($codec, $sampleRate Hz, $channelCount ch, '
      '${bitsPerSample ?? '?'} bit)';
}

/// A snapshot of the player, as every screen and the lock screen see it.
final class PlayerState {
  const PlayerState({
    this.status = PlaybackStatus.idle,
    this.playWhenReady = false,
    this.suppressed = false,
    this.queue = const [],
    this.index,
    this.position = Duration.zero,
    this.duration,
    this.format,
    this.error,
  });

  static const empty = PlayerState();

  final PlaybackStatus status;

  /// The user's intent: true after play, false after pause.
  final bool playWhenReady;

  /// Ready and asked to play, but the platform is holding the sound back for
  /// a moment (a transient loss of audio focus, an interruption).
  final bool suppressed;

  final List<QueueItem> queue;

  /// Index of the current item in [queue]; null when there is none.
  final int? index;

  /// Position when this snapshot was taken. While playing, the player's
  /// position stream extrapolates from it.
  final Duration position;
  final Duration? duration;
  final StreamFormat? format;

  /// Why the current item could not be played, if it could not.
  final String? error;

  QueueItem? get current {
    final i = index;
    return i != null && i >= 0 && i < queue.length ? queue[i] : null;
  }

  /// Sound is coming out: asked to play, ready, and not held back.
  bool get isPlaying =>
      playWhenReady && status == PlaybackStatus.ready && !suppressed;

  bool get hasNext => index != null && index! + 1 < queue.length;

  PlayerState copyWith({
    PlaybackStatus? status,
    bool? playWhenReady,
    bool? suppressed,
    List<QueueItem>? queue,
    int? Function()? index,
    Duration? position,
    Duration? Function()? duration,
    StreamFormat? Function()? format,
    String? Function()? error,
  }) => PlayerState(
    status: status ?? this.status,
    playWhenReady: playWhenReady ?? this.playWhenReady,
    suppressed: suppressed ?? this.suppressed,
    queue: queue ?? this.queue,
    index: index != null ? index() : this.index,
    position: position ?? this.position,
    duration: duration != null ? duration() : this.duration,
    format: format != null ? format() : this.format,
    error: error != null ? error() : this.error,
  );

  @override
  bool operator ==(Object other) =>
      other is PlayerState &&
      other.status == status &&
      other.playWhenReady == playWhenReady &&
      other.suppressed == suppressed &&
      _sameItems(other.queue, queue) &&
      other.index == index &&
      other.position == position &&
      other.duration == duration &&
      other.format == format &&
      other.error == error;

  @override
  int get hashCode => Object.hash(
    status,
    playWhenReady,
    suppressed,
    Object.hashAll(queue),
    index,
    position,
    duration,
    format,
    error,
  );

  @override
  String toString() =>
      'PlayerState($status, playWhenReady: $playWhenReady, '
      '${suppressed ? 'suppressed, ' : ''}'
      'index: $index/${queue.length}, position: $position, '
      'duration: $duration, error: $error)';
}

/// What the system media session shows on the lock screen and in the
/// notification; read back for tests and the debug screen.
final class MediaSessionSnapshot {
  const MediaSessionSnapshot({
    this.title,
    this.artist,
    this.album,
    this.duration,
    this.position,
    this.playbackRate,
    this.hasArtwork = false,
    this.commands = const {},
  });

  final String? title;
  final String? artist;
  final String? album;
  final Duration? duration;
  final Duration? position;
  final double? playbackRate;
  final bool hasArtwork;

  /// Offered remote commands: `play`, `pause`, `next`, `previous`, `seek`.
  final Set<String> commands;

  @override
  String toString() =>
      'MediaSessionSnapshot($title, $artist, $album, duration: $duration, '
      'position: $position, rate: $playbackRate, art: $hasArtwork, '
      'commands: $commands)';
}

bool _sameItems(List<QueueItem> a, List<QueueItem> b) {
  if (identical(a, b)) return true;
  if (a.length != b.length) return false;
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}
