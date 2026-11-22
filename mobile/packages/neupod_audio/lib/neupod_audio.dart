// SPDX-License-Identifier: MPL-2.0

/// The neuPod audio engine: a thin bridge to Media3 ExoPlayer inside a
/// MediaSessionService on Android, and to AVAudioEngine with the system media
/// controls on iOS (docs/app/decisions/0001-audio-engine.md and
/// 0002-media-session.md).
///
/// The app's audio layer (`mobile/lib/audio/`) wraps this in the domain's
/// `Player` interface; nothing else should use it directly.
library;

import 'dart:async';

import 'package:flutter/foundation.dart';

import 'package:neupod_audio/src/messages.g.dart';

export 'package:neupod_audio/src/messages.g.dart'
    show EngineEvent, EngineFormat, EngineItem, EngineStatus, SessionInfo;

/// Commands to the native engine and its stream of [EngineEvent]s.
///
/// One instance per app: the native side has one engine and one media
/// session.
class NeupodAudio {
  NeupodAudio({@visibleForTesting AudioHostApi? hostApi})
    : _host = hostApi ?? AudioHostApi() {
    AudioFlutterApi.setUp(_Receiver(_events));
  }

  final AudioHostApi _host;
  final StreamController<EngineEvent> _events =
      StreamController<EngineEvent>.broadcast();

  /// Every change of state the engine reports, in order.
  Stream<EngineEvent> get events => _events.stream;

  Future<void> setQueue(
    List<EngineItem> items, {
    int startIndex = 0,
    int startPositionMs = 0,
  }) => _host.setQueue(items, startIndex, startPositionMs);

  Future<void> insert(int index, List<EngineItem> items) =>
      _host.insert(index, items);

  Future<void> remove(int index) => _host.remove(index);

  Future<void> move(int from, int to) => _host.move(from, to);

  Future<void> clear() => _host.clear();

  Future<void> play() => _host.play();

  Future<void> pause() => _host.pause();

  Future<void> seekTo(int positionMs) => _host.seekTo(positionMs);

  Future<void> skipTo(int index) => _host.skipTo(index);

  Future<void> skipToNext() => _host.skipToNext();

  Future<void> skipToPrevious() => _host.skipToPrevious();

  Future<EngineEvent> getState() => _host.getState();

  /// What the lock screen and notification currently show.
  Future<SessionInfo> getSessionInfo() => _host.getSessionInfo();

  /// Stops listening for events. The native engine keeps running (it belongs
  /// to the media session, which outlives the Flutter engine on Android).
  Future<void> dispose() async {
    AudioFlutterApi.setUp(null);
    await _events.close();
  }
}

class _Receiver implements AudioFlutterApi {
  _Receiver(this._sink);

  final StreamController<EngineEvent> _sink;

  @override
  void onEvent(EngineEvent event) {
    if (!_sink.isClosed) _sink.add(event);
  }
}
