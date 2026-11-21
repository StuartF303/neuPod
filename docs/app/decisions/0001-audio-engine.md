# 0001: Audio engine

Status: Proposed (2026-11-21) · Owner: Audio engineer

## Context

- Formats: FLAC up to 24/192, ALAC, MP3, AAC/M4A, WAV, Opus, Ogg Vorbis, on iOS and Android. A file the platform can't decode is listed greyed with the reason, and it never stops the queue.
- True gapless between consecutive tracks, checked by a measured join test.
- Queue: play, play next, add, reorder, clear. Seek. Shuffle and repeat stay in the Dart domain layer.
- Background playback and interruptions: a call, headphones unplugged (becoming noisy), audio focus loss. The session side is in [0002](0002-media-session.md).
- No resampling or DSP in the app. Pass the file's sample rate through to an external DAC where the platform allows, and report what was achieved.
- Play from an Android SAF `content://` tree URI and from an iOS security-scoped bookmarked URL, not just plain paths.
- No network connection at all, no analytics.
- Licence compatible with distributing an MPL-2.0 app on both stores. Actively maintained.
- Minimum OS (owner, 2026-11-21): iOS 18 and Android 10 (API 29). iOS 27 shipped on 14 September 2026. Every option below supports those versions.

## Options considered

All facts were checked on 2026-11-21 against pub.dev's API, GitHub and the vendor docs listed under Sources.

| Option | Version (date) | Licence | Mins | Formats on iOS | Formats on Android | Gapless | SAF / bookmark | No network / no DSP | Maintenance |
|---|---|---|---|---|---|---|---|---|---|
| **just_audio** (+ audio_service) | 0.10.6 (2026-06-29) | MIT, bundles Media3 under Apache-2.0 | iOS 12, Android 16 | AVQueuePlayer: FLAC, ALAC, MP3, AAC, WAV. **No Ogg** | Media3 **1.4.1** (Aug 2024): all except ALAC (device-dependent) | Android yes. iOS: open reports of gaps and clicks on iOS 17+ and iOS 26, MP3 and FLAC (#1150, #1151, #1556) | `content://` passes through to Media3 `DefaultDataSource` (untested, #1383 open). iOS: file path only, the app holds the scope | Local HTTP proxy only for headers, caching and stream sources. No float or high-res output option | 4,151 likes, single maintainer, about 1 release a year lately |
| **media_kit** + media_kit_libs_audio | 1.2.6 (2025-12-13) | MIT wrapper. libmpv and FFmpeg binaries are LGPL, downloaded at build time | not checked | FFmpeg: all 7 | FFmpeg: all 7 | mpv `gapless-audio` | `content://` through an fd helper. Bookmark: path | mpv may resample unless configured. No media session, no focus handling | 919 likes, last release 11 months ago, commits Aug 2026, 372 open issues |
| **audioplayers** | 6.8.1 (2026-06-27) | MIT | n/a | platform | platform | **No playlist or gapless API** | n/a | n/a | Active |
| **flutter_soloud** | 5.3.1 (2026-11-18) | MIT / Zlib | iOS 13, Android 21 | Xiph codecs bundled, plus AudioToolbox for M4A | Xiph bundled, plus NDK MediaCodec for M4A (so no ALAC) | Sample-accurate scheduling, but no queue or player model | not documented | A game mixer that resamples to the engine rate, which breaks the no-DSP rule. No media session | Very active |
| **Custom thin plugin**: Media3 ExoPlayer (Android) and AVAudioEngine (iOS) | Media3 **1.11.1** (2026-09-11). AVFAudio ships with iOS | Our code MPL-2.0. Media3 Apache-2.0. libogg, libopus and libvorbis BSD-3 | Media3 minSdk 23. AVAudioEngine iOS 8+ | AVAudioFile: FLAC, ALAC, MP3, AAC, WAV. Bundled Xiph decoders for Ogg Opus and Ogg Vorbis | Media3: FLAC, MP3, AAC, WAV, Ogg Opus and Vorbis. ALAC needs a bundled decoder (see risks) | Android: ExoPlayer playlist. iOS: buffers scheduled back-to-back on one `AVAudioPlayerNode`, so the join is sample-accurate by construction | `content://` handled natively by Media3 `ContentDataSource`. The plugin resolves bookmarks and holds the scope | No HTTP code paths used. We control the output format and session rate | Media3 is Google's maintained stack. We maintain the plugin |

**just_audio (with audio_service).** The quickest route, and the API matches the queue we need. It fails the spec on iOS in two ways. First, it plays through `AVQueuePlayer`, and its tracker has open, unresolved reports of audible gaps between items on iOS 17 and later, including iOS 26 with FLAC. The maintainer's conclusion is that the cause is inside AVQueuePlayer ("mysterious behaviour of Apple's AVQueuePlayer", #1151). Second, iOS can't play Ogg through it at all. On Android it pins Media3 1.4.1 and offers no float or high-res output setting. An AVAudioEngine rewrite of the iOS side has been an open request since 2021 (#334) and has no delivery date.

**media_kit.** The only off-the-shelf option that decodes all seven formats on both platforms, because it ships FFmpeg. Against it: no media session (it would need audio_service), no handling of interruptions or audio focus, mpv's own output and resampling chain, about 10 MB or more of LGPL native binaries fetched from GitHub at build time, LGPL relinking obligations in App Store builds, and 11 months without a pub release. It is the fallback if bundling Xiph and ALAC decoders ourselves proves too costly.

**flutter_soloud and audioplayers.** Not music-player engines. audioplayers has no gapless queue. SoLoud is a game mixer and resamples by design.

**Custom thin plugin.** On Android, Media3 does the work: gapless playlists, `content://`, audio focus, becoming noisy, and the session (0002). On iOS, AVAudioEngine scheduling makes gapless independent of AVQueuePlayer's behaviour. Bundled Xiph decoders close the Ogg gap that the system frameworks leave. The cost is native code for us to own on both platforms: route-change and engine-restart handling, seek by rescheduling, and position reporting.

## Decision

Build **a thin in-repo Flutter plugin** (`mobile/packages/neupod_audio`, MPL-2.0) with a small Dart API: set queue, insert, move, remove, clear, play, pause, seek, skip, and streams of state, position, current index and format.

- **Android:** `androidx.media3:media3-exoplayer` 1.11.x, with the player owned by a Media3 `MediaSessionService` (see 0002).
- **iOS:** `AVAudioEngine` with one `AVAudioPlayerNode`. Decoding uses `AVAudioFile` for FLAC, ALAC, MP3, AAC and WAV, and bundled libogg, libopus and libvorbis for Ogg. Each track's PCM buffers are scheduled back-to-back. The session's preferred sample rate is set to the file's rate.

Why: it is the only option that can meet gapless on iOS, and all seven formats on both platforms, without LGPL binaries or an unmaintained dependency. Media3 does most of the Android work and is maintained by Google.

The phase-1 spike first runs the gapless join test on iOS with a plain AVQueuePlayer. If it passes for every format, the iOS side may use AVQueuePlayer for the system formats, which handles AirPlay and route changes for us, and keep AVAudioEngine only for Ogg.

## Consequences and risks

- **ALAC on Android isn't a platform format.** It is missing from Android's supported-formats table, so it is device-dependent. Meeting the spec needs a bundled decoder. Either:
  - Media3's FFmpeg extension built with only `alac`. Google documents this path, but it is FFmpeg, LGPL-2.1, and we must build it from source.
  - Apple's open-source ALAC decoder (Apache-2.0) wrapped as a Media3 decoder. More code for us.

  **Owner or lead choice needed.**
- **Ogg Vorbis and Ogg Opus on iOS have no system decoder for apps.** No Apple document lists them. A third-party test on the iOS 27 simulator found that AVFoundation demuxes Ogg but has no Vorbis decoder. The same project reported Ogg Opus working on iOS 27, then reverted that change a day later. Safari's Ogg support since iOS 18.4 is WebKit's own and isn't exposed to apps. We meet the spec only by bundling the Xiph decoders, about 1 MB, BSD-3.
- **24-bit/192 kHz FLAC on Android.** The platform FLAC decoder is documented only to 48 kHz, "no dither applied for 24-bit". If the spike shows real decoders fall short, add Media3's libflac extension (BSD, built from source), which outputs raw PCM on every API level. Whether 24-bit PCM reaches the AudioTrack or is reduced to 16-bit by `DefaultAudioSink` must be checked. We may need float output enabled.
- **Sample-rate passthrough and bit-perfect output.** No option can guarantee these. The platforms limit them:
  - **Android 10–13:** the system mixer resamples everything to the output's rate, usually 48 kHz.
  - **Android 14+:** `setPreferredMixerAttributes` with `MIXER_BEHAVIOR_BIT_PERFECT` exists for **USB devices only**, and vendor HAL support is optional.
  - **iOS:** `setPreferredSampleRate` is only a hint. Some USB DACs follow it; reports differ on whether output goes above 48 kHz.
  - **Bluetooth and AirPlay:** always re-encoded or at a fixed rate.

  The format line must report the achieved output rate, never a claim.
- **MP3 and AAC gapless** depends on encoder-delay metadata (LAME/Xing, iTunSMPB) being present and honoured. Files without it can't be gapless on any engine.
- **iOS AVAudioEngine** stops on route and configuration changes, and the plugin must restart it and reschedule from the current frame. That is a source of bugs; test it on the device with Bluetooth, wired and AirPlay.
- **The Mac's toolchain** is Xcode 26.5, which has no iOS 27 SDK or simulator, and phones in use run iOS 27. iOS 27 behaviour, such as Ogg in AVFoundation, can only be checked on a device, or after Xcode 27 is installed. Ask the owner.
- We own native code in Kotlin and Swift. Media3 is Apache-2.0 and the Xiph libraries are BSD-3; both go on the Licences screen and in `LICENSE.md`.

## To verify in the phase 1 spike

1. Gapless: run the generated two-track join fixture (FLAC 16/44.1, FLAC 24/96, ALAC, AAC with iTunSMPB, MP3 with LAME header, Opus, Vorbis, WAV) through the iOS AVQueuePlayer and AVAudioEngine variants and through Media3. Measure the sample offset at the join, not by ear. Pass means 0 samples of inserted silence.
2. Play `content://` FLAC from a persisted SAF tree grant on an API 29 emulator and an API 36 emulator, after an app restart.
3. Play a FLAC from an iOS security-scoped bookmark to an iCloud Drive or external folder after an app relaunch, on the owner's iPhone.
4. Decode every fixture in `tests/assets/audio` on both platforms. Record per format: played, or the exact error. Include 24/192 FLAC on Android using the platform decoder only, and ALAC on the emulator and a real Android phone.
5. Android output encoding for 24-bit FLAC: log the AudioTrack encoding and sample rate. On an Android 14+ phone with a USB DAC, try `setPreferredMixerAttributes(BIT_PERFECT)` and record the result.
6. iOS: set the preferred sample rate to 96 kHz with a USB DAC, and log `AVAudioSession.sampleRate` after activation.
7. Check that the release Android manifest has no `INTERNET` permission, and that nothing in the plugin opens a socket. On iOS, record a network trace during playback (it should be empty).
8. Build size delta of the Xiph decoders, and of the ALAC decoder option chosen.

## Sources

All checked 2026-11-21.

- pub.dev API: [just_audio](https://pub.dev/packages/just_audio), [just_audio_background](https://pub.dev/packages/just_audio_background), [audio_service](https://pub.dev/packages/audio_service), [media_kit](https://pub.dev/packages/media_kit), [media_kit_libs_audio](https://pub.dev/packages/media_kit_libs_audio), [audioplayers](https://pub.dev/packages/audioplayers), [flutter_soloud](https://pub.dev/packages/flutter_soloud)
- just_audio source (branch `minor`): [build.gradle.kts (Media3 1.4.1)](https://github.com/ryanheise/just_audio/blob/minor/just_audio/android/build.gradle.kts), [README feature table](https://github.com/ryanheise/just_audio/blob/minor/just_audio/README.md), [CHANGELOG](https://github.com/ryanheise/just_audio/blob/minor/just_audio/CHANGELOG.md)
- just_audio issues: [#1151 iOS 17+ gaps (AVQueuePlayer)](https://github.com/ryanheise/just_audio/issues/1151), [#1150](https://github.com/ryanheise/just_audio/issues/1150), [#1556 gap on iOS 26 (FLAC, MP3)](https://github.com/ryanheise/just_audio/issues/1556), [#1383 SAF content://](https://github.com/ryanheise/just_audio/issues/1383), [#334 AVAudioEngine rewrite](https://github.com/ryanheise/just_audio/issues/334), [#53 Ogg](https://github.com/ryanheise/just_audio/issues/53)
- media_kit: [repo](https://github.com/media-kit/media-kit), [Android content:// fd helper](https://github.com/media-kit/media-kit/blob/main/libs/android/media_kit_libs_android_audio/android/src/main/java/com/alexmercerind/mediakitandroidhelper/MediaKitAndroidHelper.java), [libmpv Android audio build](https://github.com/media-kit/libmpv-android-audio-build)
- [flutter_soloud README](https://github.com/alnitak/flutter_soloud), [audioplayers repo](https://github.com/bluefireteam/audioplayers)
- Media3: [ExoPlayer supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats), [Google Maven media3-exoplayer 1.11.1 (minSdk 23, Apache-2.0)](https://maven.google.com/web/index.html#androidx.media3:media3-exoplayer), [release notes](https://developer.android.com/jetpack/androidx/releases/media3)
- Android: [Supported media formats](https://developer.android.com/media/platform/supported-formats), [AudioMixerAttributes (API 34)](https://developer.android.com/reference/android/media/AudioMixerAttributes), [Preferred mixer attributes on USB devices](https://source.android.com/docs/core/audio/preferred-mixer-attr)
- iOS: [Jellify PR #1547 (Ogg on iOS 27 sim, AVFoundation has no Vorbis decoder)](https://github.com/Jellify-Music/App/pull/1547), [MacGeneration: Ogg Vorbis in iOS 18.4 via Safari/WebKit](https://www.macg.co/macos/2026/02/apple-ajoute-le-support-de-logg-vorbis-en-douce-dans-macos-et-ios-avec-une-lecture-native-306585), [QA1631 preferred sample rate is a hint](https://developer.apple.com/library/ios/qa/qa1631/_index.html), [kAudioFormatFLAC](https://developer.apple.com/documentation/coreaudiotypes/kaudioformatflac), [iOS 27 release notes](https://developer.apple.com/documentation/ios-ipados-release-notes/ios-ipados-27-release-notes)
