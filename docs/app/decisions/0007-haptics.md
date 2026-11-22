# 0007: Haptics and the system volume

Status: Proposed (2026-11-22) · Owner: UI engineer 2

## Context

- Owner decision 9: wheel detents are light haptic ticks and button presses are firmer haptic clicks. Owner decision 10:
  on Now Playing the wheel sets the phone's system volume.
- The device view design (`docs/design/2026-11-21-app-device-view`, section 5) names a platform API for each haptic
  event and asks for the names to be checked against current documentation. It also asks for one generator per kind,
  prepared in advance on iOS, at most 30 detent ticks a second, and respect for the system setting and an in-app
  **Haptics** setting.
- Minimum OS (owner): iOS 18, Android 10 (API 29). Toolchain: Xcode with the iOS 27 SDK, Android compileSdk 36.

## What the documentation says

Checked 2026-11-22. Apple's pages were read through their documentation JSON, which the HTML pages load.

| Event (design) | iOS, as designed | Checked | Android, as designed | Checked |
| --- | --- | --- | --- | --- |
| Detent | `UISelectionFeedbackGenerator.selectionChanged()` | iOS 10, current. The docs say: use it "only when the selection changes", which fits a detent | `CLOCK_TICK`; `SEGMENT_FREQUENT_TICK` where available | `CLOCK_TICK` is API 21. `SEGMENT_FREQUENT_TICK` is API 34: "very soft … if the device can't make a suitably soft vibration, then it may not make any vibration" |
| Button or centre down | `UIImpactFeedbackGenerator(style: .rigid)`, intensity 0.8 | `.rigid` is iOS 13; `impactOccurred(intensity:)` is iOS 13, range 0 to 1 | `VIRTUAL_KEY` | API 5. Android's own key-press example uses it on down |
| Button or centre up | `.light`, intensity 0.5 | iOS 10 | `VIRTUAL_KEY_RELEASE` | API 27, so it is available at our minimum. Used on up in the same example |
| A hold takes effect | `.medium` | iOS 10 | `LONG_PRESS` | API 3 |
| End of a list | `.soft`, once | iOS 13 | `REJECT`, once | API 30, which is **above our minimum of 29** |

Two corrections to the design's table:

1. **iOS initialisers are deprecated in the iOS 27 SDK.** `UIImpactFeedbackGenerator.init(style:)` and
   `UIFeedbackGenerator.init()` are marked deprecated in iOS 27.0. `UISelectionFeedbackGenerator()` uses the latter, so
   it is deprecated too. The replacements are `init(style:view:)` and `init(view:)`, both iOS 17.5. Our minimum is iOS
   18, so the plugin uses the view-attached forms directly, with no availability check. The generator classes and
   `selectionChanged()` / `impactOccurred(intensity:)` are not deprecated.
2. **`REJECT` needs a fallback on Android 10.** On API 29 the end of a list uses `LONG_PRESS`, the same as a hold.

One clarification: the table's "API 34" applies only to `SEGMENT_FREQUENT_TICK`; `CLOCK_TICK` is API 21.

**System setting.**
- iOS: the generators only ask. The system plays the haptic only on hardware that has it, with the app in the
  foreground, and with the system Haptics setting on.
- Android: `View.performHapticFeedback` honours the user's touch-feedback setting by default and needs no `VIBRATE`
  permission. Since API 33 only privileged apps can override that setting (`FLAG_IGNORE_GLOBAL_SETTING` is
  deprecated); we never ask to.

So obeying the system setting needs no code. The in-app Haptics setting is applied in Dart, before any call.

**Preparing (iOS).** `prepare()` is optional but recommended. The Taptic Engine stays ready "for a short period of time
(typically seconds)". Calling it immediately before a haptic does not help. Prepare again after each haptic when more
are likely.

**Volume.**
- Android: `AudioManager.adjustStreamVolume(STREAM_MUSIC, ADJUST_RAISE | ADJUST_LOWER, 0)` moves one step. Flags 0
  omits `FLAG_SHOW_UI`, so the system volume panel is not asked for. It does nothing on fixed-volume devices
  (`isVolumeFixed()`, API 21). The step count is `getStreamMaxVolume` less `getStreamMinVolume` (API 28).
- iOS: there is **no public API** to set the system volume. `AVAudioSession.outputVolume` is read-only. Apple DTS
  (2020) called digging into `MPVolumeView`'s view hierarchy unreliable. Apple WWDR (August 2026) says a program
  cannot change the system volume without user interaction. Developers report the hidden-slider trick still in use,
  but I found no confirmation that setting the slider works on iOS 18, 26 or 27. "16 steps, 1/16 each" is community
  knowledge, not documented.

## Decision

An in-repo plugin, **`mobile/packages/neupod_device`** (Kotlin and Swift through Pigeon 29, as Flutter 3.50's plugin
template does), covers both haptics and volume. It was named `neupod_device`, not `neupod_haptics`, because it carries
both.

| Event | iOS | Android |
| --- | --- | --- |
| Detent | `UISelectionFeedbackGenerator(view:)`, `selectionChanged()` | `SEGMENT_FREQUENT_TICK` on API 34+, else `CLOCK_TICK` |
| Down | `UIImpactFeedbackGenerator(style: .rigid, view:)`, `impactOccurred(intensity: 0.8)` | `VIRTUAL_KEY` |
| Up | `UIImpactFeedbackGenerator(style: .light, view:)`, intensity 0.5 | `VIRTUAL_KEY_RELEASE` |
| Hold took effect | `.medium` | `LONG_PRESS` |
| End of a list | `.soft` | `REJECT` on API 30+, else `LONG_PRESS` |

- **iOS generators.** One per kind, attached to the key window. They are made and prepared when the wheel is touched,
  and the selection and light generators are prepared again after each use.
- **Android.** The plugin calls `performHapticFeedback` on the activity's decor view.
- **Rate limit and setting.** In Dart (`DeviceFeedback`): detents at most 30 a second, the rest dropped; nothing at all
  when the in-app Haptics setting is off. A press cancelled by becoming a turn gets no release haptic.
- **Volume.**
  - Android: `adjustStreamVolume` with flags 0.
  - iOS: a hidden `MPVolumeView` inside the key window, off screen at alpha 0.01, whose `UISlider` is set to the next
    1/16 step. It is in the window because the slider only works there and the system HUD stays away only then.
  - Both return the new step and the step count, for the device's volume bar.
- **Click sound.** A seam only (`ClickSoundPort`, silent). The setting exists (Off by default) and nothing plays yet.
- **Verifying.** Emulators and simulators cannot vibrate, so every call is logged: Android under the tag
  `neupod_device` (`adb logcat -s neupod_device`), iOS under subsystem `net.stuartfraser.neupod`, category `device`.
  The feel is for the owner's device check.

## Consequences and risks

- **The iOS volume workaround is unsupported by Apple and may not work on iOS 27.** Owner decision 10 says: if Apple
  breaks it, the fallback is to be asked for, not guessed. This must be checked on the owner's iPhone (iOS 27) at the
  first device check. If it fails, the step reports the target but the volume does not move. Options for the owner
  then: a visible `MPVolumeView` on Now Playing, an app-level volume on the player, or no wheel volume on iOS.
- It is also an App Review risk, which stays with the owner at submission, like Open question 1 of the spec.
- `SEGMENT_FREQUENT_TICK` may be silent on phones whose motor cannot make a very soft tick. If the owner's Pixel feels
  nothing, switch the detent to `CLOCK_TICK` on all versions. That is a one-line change in `NeupodDevicePlugin.kt`.
- Android's `REJECT` fallback on API 29 feels the same as a hold.

## Sources

Checked 2026-11-22.

- UISelectionFeedbackGenerator: <https://developer.apple.com/documentation/uikit/uiselectionfeedbackgenerator>
- UIImpactFeedbackGenerator: <https://developer.apple.com/documentation/uikit/uiimpactfeedbackgenerator>.
  `init(style:)`, deprecated 27.0: <https://developer.apple.com/documentation/uikit/uiimpactfeedbackgenerator/init(style:)>.
  `init(style:view:)`: <https://developer.apple.com/documentation/uikit/uiimpactfeedbackgenerator/init(style:view:)>.
  `impactOccurred(intensity:)`: <https://developer.apple.com/documentation/uikit/uiimpactfeedbackgenerator/impactoccurred(intensity:)>
- UIFeedbackGenerator `init()`, deprecated 27.0: <https://developer.apple.com/documentation/uikit/uifeedbackgenerator/init()>.
  `init(view:)`: <https://developer.apple.com/documentation/uikit/uifeedbackgenerator/init(view:)>.
  `prepare()`: <https://developer.apple.com/documentation/uikit/uifeedbackgenerator/prepare()>
- When iOS plays haptics: <https://developer.apple.com/documentation/applepencil/playing-haptic-feedback-in-your-app>.
  HIG, Playing haptics: <https://developer.apple.com/design/human-interface-guidelines/playing-haptics>
- HapticFeedbackConstants: <https://developer.android.com/reference/android/view/HapticFeedbackConstants>.
  `View.performHapticFeedback`: <https://developer.android.com/reference/android/view/View#performHapticFeedback(int)>
- Android haptic feedback guide: <https://developer.android.com/develop/ui/views/haptics/haptic-feedback>.
  Haptics design principles: <https://developer.android.com/develop/ui/views/haptics/haptics-principles>
- AudioManager: <https://developer.android.com/reference/android/media/AudioManager>
- `AVAudioSession.outputVolume`: <https://developer.apple.com/documentation/avfaudio/avaudiosession/outputvolume>.
  MPVolumeView: <https://developer.apple.com/documentation/mediaplayer/mpvolumeview>.
  `MPMusicPlayerController.volume`, deprecated iOS 7: <https://developer.apple.com/documentation/mediaplayer/mpmusicplayercontroller/volume>
- Apple forums: <https://developer.apple.com/forums/thread/649183> (DTS, 2020), <https://developer.apple.com/forums/thread/820665>
  (WWDR, 2026), <https://developer.apple.com/forums/thread/756484>, <https://developer.apple.com/forums/thread/813242>
