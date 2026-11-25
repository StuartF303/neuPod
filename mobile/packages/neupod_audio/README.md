# neupod_audio

The neuPod app's audio engine and media session, as a thin in-repo Flutter plugin (decisions
[0001](../../../docs/app/decisions/0001-audio-engine.md) and [0002](../../../docs/app/decisions/0002-media-session.md)).
Used only by `mobile/lib/audio/`. MPL-2.0.

| | Android | iOS |
| --- | --- | --- |
| Player | Media3 ExoPlayer 1.11.1 in a `MediaSessionService` (`PlaybackService.kt`) | `AVAudioEngine` with one `AVAudioPlayerNode`; the current and next tracks scheduled back to back (`AudioEngine.swift`) |
| URIs | `content://` and `file://` only (`LocalDataSource.kt`; no HTTP, UDP or RTMP data source) | `file://` under a root kept accessible by `neupod_storage` |
| Lock screen, notification, buttons | Media3 session and its MediaStyle notification | `MPNowPlayingInfoCenter` and `MPRemoteCommandCenter` (`NowPlaying.swift`) |
| Background | foreground service of type `mediaPlayback` (this package's manifest) | `UIBackgroundModes: audio` in the app's `Info.plist`, session category `.playback` |

The Dart API (`lib/neupod_audio.dart`) is a queue (set, insert, remove, move, clear), transport (play, pause, seek,
skip), and a stream of `EngineEvent` snapshots; the position is extrapolated on the Dart side. The contract is
`pigeon/messages.dart`; regenerate with `dart run pigeon --input pigeon/messages.dart` then `dart format lib`.

Not yet: Ogg Opus and Vorbis on iOS (bundled Xiph decoders, phase 4), ALAC on Android (Apple's decoder as a Media3
decoder, phase 4), playback resumption from the system UI, interruption resume rules and the output route (phase 4).
