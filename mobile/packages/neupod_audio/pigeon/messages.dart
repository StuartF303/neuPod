// SPDX-License-Identifier: MPL-2.0

// The Dart <-> native contract of the neuPod audio engine (decisions 0001 and
// 0002 in docs/app/decisions/). After editing, regenerate from
// mobile/packages/neupod_audio:
//
//   dart run pigeon --input pigeon/messages.dart
//   dart format lib
//
// Times are milliseconds. Indexes are positions in the native queue, which
// always mirrors the order the Dart side set (shuffle and repeat are Dart
// rules, decision 0001).

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(
  PigeonOptions(
    dartPackageName: 'neupod_audio',
    dartOut: 'lib/src/messages.g.dart',
    dartOptions: DartOptions(),
    copyrightHeader: 'pigeon/copyright.txt',
    kotlinOut: 'android/src/main/kotlin/net/stuartfraser/neupod/neupod_audio/Messages.g.kt',
    kotlinOptions: KotlinOptions(
      package: 'net.stuartfraser.neupod.neupod_audio',
    ),
    swiftOut: 'ios/neupod_audio/Sources/neupod_audio/Messages.g.swift',
  ),
)
/// What the engine is doing, independent of whether the user asked to play.
enum EngineStatus {
  /// Nothing loaded, or stopped after an error.
  idle,

  /// Opening or buffering the current item.
  loading,

  /// Ready: playing if [EngineEvent.playWhenReady], otherwise paused.
  ready,

  /// The last item in the queue has finished.
  ended,
}

/// One entry of the queue. [uri] is `content://` on Android or `file://` on
/// iOS (decision 0004); the engine never opens anything else.
class EngineItem {
  EngineItem({required this.id, required this.uri});

  /// Opaque to the engine; echoed back in [EngineEvent.itemId].
  String id;
  String uri;
  String? title;
  String? artist;
  String? album;

  /// Local artwork (`file://` or `content://`) for the lock screen and the
  /// notification. When null the engine may use art embedded in the file.
  String? artUri;

  /// Known duration, shown before the file is opened.
  int? durationMs;
}

/// The decoded stream as the engine reports it. Fields are null when the
/// platform does not say.
class EngineFormat {
  /// For example `audio/flac` (Android MIME type) or `flac` (iOS).
  String? codec;
  int? sampleRate;
  int? channelCount;
  int? bitsPerSample;
}

/// A snapshot of the engine, sent on every change. Position is not streamed:
/// the Dart side extrapolates from [positionMs] while playing.
class EngineEvent {
  EngineEvent({
    required this.status,
    required this.playWhenReady,
    required this.positionMs,
    required this.queueLength,
  });

  EngineStatus status;

  /// The user's intent: true after play, false after pause.
  bool playWhenReady;

  /// Index of the current item, null when the queue is empty.
  int? index;
  String? itemId;
  int positionMs;
  int? durationMs;
  int queueLength;
  EngineFormat? format;

  /// Set when the current item failed to open or decode.
  String? errorMessage;
}

/// What the system media session shows (the lock screen and the
/// notification), for tests and the debug screen.
class SessionInfo {
  SessionInfo({required this.hasArtwork});

  String? title;
  String? artist;
  String? album;
  int? durationMs;
  int? positionMs;
  double? playbackRate;
  bool hasArtwork;

  /// Remote commands the session currently offers, e.g. `play`, `pause`,
  /// `next`, `previous`, `seek`.
  List<String>? commands;
}

@HostApi()
abstract class AudioHostApi {
  /// Replaces the queue and prepares [startIndex] at [startPositionMs]. The
  /// play intent is kept: if playing, the new queue plays; if paused, it
  /// stays paused. An empty list is [clear].
  @async
  void setQueue(List<EngineItem> items, int startIndex, int startPositionMs);

  /// Inserts [items] before [index] (index == length appends).
  @async
  void insert(int index, List<EngineItem> items);

  @async
  void remove(int index);

  @async
  void move(int from, int to);

  /// Empties the queue, stops, and drops the play intent.
  @async
  void clear();

  @async
  void play();

  @async
  void pause();

  @async
  void seekTo(int positionMs);

  /// Jumps to the start of [index].
  @async
  void skipTo(int index);

  @async
  void skipToNext();

  /// After the first three seconds of a track this restarts it; otherwise it
  /// goes to the previous track (the spec's skip-back rule, the same rule the
  /// lock screen's previous button follows).
  @async
  void skipToPrevious();

  @async
  EngineEvent getState();

  @async
  SessionInfo getSessionInfo();
}

@FlutterApi()
abstract class AudioFlutterApi {
  void onEvent(EngineEvent event);
}
