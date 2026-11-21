# 0002: Media session, background playback and output route

Status: Proposed (2026-11-21) · Owner: Audio engineer

## Context

- Playback continues with the screen locked and the app in the background, on both platforms.
- Lock-screen and notification controls with artwork and a scrubber: play/pause, next, previous, seek.
- Headset and Bluetooth transport buttons.
- Interruptions: a call pauses, unplugging headphones (becoming noisy) pauses, and another app taking audio focus pauses.
- Resume: the queue, track and position are restored on reopen. That lives in the domain layer; the session only has to accept a restored queue.
- An icon shows the output in use: speaker, wired, Bluetooth or AirPlay. No Cast in R1 (owner decision 6).
- No network: artwork comes from local files only. Only the permissions needed.
- Fits the engine chosen in [0001](0001-audio-engine.md): Media3 ExoPlayer on Android, AVAudioEngine on iOS.
- Minimum OS (owner, 2026-11-21): iOS 18, Android 10 (API 29).

## Options considered

All facts were checked on 2026-11-21.

| Option | Version (date) | Licence | Mins | Works with 0001 | Lock screen and notification | Buttons | Interruptions and noisy | Route detection | Network code pulled in | Maintenance |
|---|---|---|---|---|---|---|---|---|---|---|
| **Media3 session** (Android) and **MediaPlayer/AVFAudio frameworks** (iOS), inside the 0001 plugin | media3-session 1.11.1 (2026-09-11). iOS system frameworks | Apache-2.0. OS frameworks | API 23. iOS 7.1+ (`MPRemoteCommandCenter`) | Native: the session wraps the same ExoPlayer | Yes. Media3 builds the MediaStyle notification. iOS uses `MPNowPlayingInfoCenter` with artwork, and `changePlaybackPositionCommand` for scrubbing | Yes. Media3 media-button handling; iOS `MPRemoteCommandCenter` | ExoPlayer `handleAudioFocus` and `setHandleAudioBecomingNoisy`. iOS `AVAudioSession` interruption and route-change notifications | We write it: Android `AudioManager`, iOS `AVAudioSession.currentRoute` | `DefaultHttpDataSource` is linked but unused | Google-maintained, about monthly releases |
| **audio_service** | 0.18.19 (2026-06-29). The 0.18.20 fixes are on the branch, unpublished | MIT | iOS 13, Android 19 | Via a Dart `BaseAudioHandler` bridging to our engine. On Android that puts a second session layer (legacy `androidx.media` 1.7.0) beside Media3 | Yes | Yes | Through audio_session, wired up by the app | No | **flutter_cache_manager** (http, for `http` art URIs) | 1,335 likes, single maintainer, Android lifecycle issues open (#1180, #996, #1148) |
| **just_audio_background** | 0.0.1-beta.17 (2025-05-13) | MIT | as audio_service | **No**, tied to just_audio | Yes | Yes | Via just_audio | No | as audio_service | Beta, 18 months since a release |
| **audio_session** (as a helper) | 0.2.4 (2026-06-29) | MIT | not checked (below proposals) | Could configure the session from Dart, but two owners of `AVAudioSession` and audio focus invites conflicts | n/a | n/a | Interruption and becoming-noisy streams | `getDevices()`. On Android it lists all connected devices, not the active route | none | Active |

**Native, inside the engine plugin (recommended).** Google's guidance for Media3 is to put the `ExoPlayer` and a `MediaSession` inside a `MediaSessionService`. That gives:

- the foreground service and the MediaStyle notification, populated from session metadata (from API 33 the system renders it);
- Bluetooth and headset buttons;
- playback resumption from the system UI;
- one consistent state, with no bridging.

On iOS the same plugin owns:

- the `AVAudioSession` (`.playback` category, `UIBackgroundModes: audio`);
- `MPNowPlayingInfoCenter`, with title, artist, album, duration, elapsed time, rate and `MPMediaItemArtwork` from the local art cache;
- `MPRemoteCommandCenter`: play, pause, toggle, next, previous, and change playback position for lock-screen scrubbing;
- interruption and route-change notifications. Old-device-unavailable means headphones were unplugged, so pause.

**audio_service** is the standard Flutter answer and works with any engine. Here it would duplicate on Android what Media3 already provides, behind a legacy compat session. It would add `flutter_cache_manager` and its HTTP stack to a no-network app. It also carries open Android engine-lifecycle bugs. It stays as the fallback if the team ever drops the custom engine for just_audio (0001's alternative).

## Decision

Implement the media session **natively inside the `neupod_audio` plugin from 0001**:

- **Android:** `androidx.media3:media3-session` 1.11.x, with a `MediaSessionService` that owns the ExoPlayer. Manifest entries: `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PLAYBACK`, a service with `foregroundServiceType="mediaPlayback"`, and the Media3 `MediaButtonReceiver` with `onPlaybackResumption` backed by the persisted queue.
- **iOS:** AVFAudio's `AVAudioSession` and MediaPlayer's `MPNowPlayingInfoCenter` and `MPRemoteCommandCenter`.
- **Output route:**
  - iOS: `AVAudioSession.currentRoute.outputs` port type (built-in speaker, headphones, Bluetooth A2DP/LE, AirPlay, USB, car), refreshed on `routeChangeNotification`.
  - Android 13+ (API 33+): `AudioManager.getDevicesForAttributes(USAGE_MEDIA)`.
  - Android 10–12: the best available signal, `AudioDeviceCallback` plus `isBluetoothA2dpOn()`/wired checks, as a documented approximation.

Why: one owner of player, session and focus per platform, no bridging layer, no HTTP-capable dependency, and both sides are maintained by Google or are OS frameworks.

## Consequences and risks

- **"Cast" can't be supported on Android in R1.** Casting from an app needs the Google Cast SDK and the app's own network connection, which the R1 no-network rule forbids. The Android icon set is therefore speaker, wired, Bluetooth and USB. **Resolved: owner decision 6 (README.md), no Cast in R1.** iOS AirPlay is fine: the OS does the networking, and the route shows as `airPlay`. The AVAudioEngine path must use `routeSharingPolicy = .longFormAudio` for AirPlay 2, which the spike will verify.
- **The active route on Android 10–12 is inferred, not reported.** No public API before API 33 returns the device the media stream is routed to. A rare mismatch is possible, for example Bluetooth connected but audio forced to the speaker.
- Artwork must be passed as a file or `content://` URI or as bytes, never an `http` URI. The Android release manifest has no `INTERNET` permission, so the OS enforces no network.
- Android 13+ media-session notifications are exempt from the `POST_NOTIFICATIONS` runtime permission. No extra prompt is expected; the spike will confirm on API 36.
- We write and test the Dart ↔ native contract ourselves: state, position and queue-index streams.
- iOS: the plugin must re-publish the now-playing info after every AVAudioEngine restart, or the lock screen goes stale.

## To verify in the phase 1 spike

1. Android API 29 and API 36 emulators: start playback, lock, and check that the notification and lock screen show art, title and a working seek bar. Swipe the app away and check that playback continues.
2. iPhone (owner's): lock-screen and Control Centre show art and title, and the scrubber seeks to the right position. Playback survives 30 minutes locked.
3. Headset and Bluetooth: single press toggles, double press goes next, triple press goes previous, on both platforms. On Android also test playback resumption from a Bluetooth play press after the service has died.
4. Interruptions: an incoming call (simulated on both emulators/simulators, real on the device) pauses and doesn't auto-resume unless the system says so. Unplugging wired headphones or disconnecting Bluetooth pauses. Another media app taking focus pauses.
5. Route icon: speaker, wired, Bluetooth, USB DAC (Android) and AirPlay (iOS) each show correctly. Switching mid-track updates the icon within 1 s and, on iOS, playback continues after the engine restart.
6. Confirm no `POST_NOTIFICATIONS` prompt is needed on API 33+ for the media notification.

## Sources

All checked 2026-11-21.

- [Media3 background playback with MediaSessionService](https://developer.android.com/media/media3/session/background-playback): foreground permissions, automatic notification, media buttons, resumption.
- [Google Maven: androidx.media3 (media3-session 1.11.1)](https://maven.google.com/web/index.html#androidx.media3:media3-session), [Media3 release notes](https://developer.android.com/jetpack/androidx/releases/media3)
- [AudioManager.getDevicesForAttributes](https://developer.android.com/reference/android/media/AudioManager#getDevicesForAttributes(android.media.AudioAttributes)), [Google Cast SDK for Android senders](https://developers.google.com/cast/docs/android_sender)
- Apple: [MPNowPlayingInfoCenter](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter), [MPRemoteCommandCenter](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter), [changePlaybackPositionCommand](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter/changeplaybackpositioncommand), [AVAudioSession.routeChangeNotification](https://developer.apple.com/documentation/avfaudio/avaudiosession/routechangenotification), [AVAudioSession.RouteSharingPolicy.longFormAudio](https://developer.apple.com/documentation/avfaudio/avaudiosession/routesharingpolicy-swift.enum/longformaudio)
- pub.dev: [audio_service](https://pub.dev/packages/audio_service) (0.18.19, MIT, iOS 13 / Android minSdk 19, depends on flutter_cache_manager), [just_audio_background](https://pub.dev/packages/just_audio_background) (0.0.1-beta.17), [audio_session](https://pub.dev/packages/audio_session) (0.2.4)
- audio_service source and issues: [pubspec and CHANGELOG on `minor`](https://github.com/ryanheise/audio_service/tree/minor/audio_service), [#1180](https://github.com/ryanheise/audio_service/issues/1180), [#996](https://github.com/ryanheise/audio_service/issues/996), [#1148](https://github.com/ryanheise/audio_service/issues/1148), [#1178 content: artwork fd leak](https://github.com/ryanheise/audio_service/issues/1178)
- [audio_session core.dart (getDevices semantics)](https://github.com/ryanheise/audio_session/blob/master/lib/src/core.dart)
