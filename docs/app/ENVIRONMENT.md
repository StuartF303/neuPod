# Build environment: neuPod phone app

The machine the app is built and tested on, as checked at the start of phase 0. Update it when the toolchain changes.

Checked 2026-11-21 on the owner's Mac, over ssh in `tmux`.

| Part | Version | Notes |
| --- | --- | --- |
| Mac | Apple M1, 8 GB RAM, macOS 26.5.1 (25F80) | 8 GB is tight: run one emulator or simulator and one build at a time |
| Flutter | 3.50.1 stable (framework `ae9b586f34`, 2026-11-18), Dart 3.14.0 | Homebrew cask `flutter` |
| Xcode | 26.5 (17F42), command-line tools at `/Applications/Xcode.app/Contents/Developer` | First launch done |
| CocoaPods | 1.17.0 | Homebrew |
| iOS simulator | iOS 26.5 runtime (23F77): iPhone 17, 17 Pro, 17 Pro Max, 17e, Air | No iOS 18 runtime installed yet (the minimum is iOS 18, see below) |
| JDK | Temurin 17.0.21.1, set with `flutter config --jdk-dir` | A Homebrew OpenJDK 21 is also on `PATH`; Flutter is pinned to 17 to match CI |
| Android SDK | `~/Library/Android/sdk`: platforms 35 and 36, build-tools 35.0.0 and 36.0.0, platform-tools 37.0.3, emulator 37.3.10, cmdline-tools 21.0 | Licences accepted |
| Android emulator | AVD `neupod_api36`: Pixel 8, API 36, Google APIs, arm64-v8a | |
| GitHub CLI | 2.105.0, signed in, scopes include `repo` and `workflow` | |
| tmux | 3.8 | |

`flutter doctor -v`: every check passes. Its only warning is that it can't browse the network for the owner's iPhone, which
is expected while the phone is not attached.

## Set-up notes

- `scripts/app/bootstrap-mac.sh --no-agent` finishes cleanly. On the first run, `brew install` failed with "Cannot link
  ca-certificates" and then "Cannot link ruby", because Homebrew had left an old version of each package (and of
  libevent, libunistring, pcre2 and utf8proc) beside the new one. `brew cleanup <formula>` on each fixed it.
- The `temurin@17` cask runs a `.pkg` installer that needs the owner's password, which can't be typed at an ssh prompt
  without a terminal. It was run in a separate `tmux` window and the owner typed the password there.
- The self-hosted GitHub Actions runner on this Mac (`~/actions-runner`) belongs to another project. The app's CI does not
  use it; iOS CI runs on GitHub-hosted macOS runners.

## Owner answers that set the targets (2026-11-21)

- Bundle and application ID: `net.stuartfraser.neupod` (spec Open question 4).
- Minimum OS: iOS 18, Android 10 (API 29) (Open question 5).
- Android test phone: the owner's Pixel; model and Android version still to be recorded (Open question 6).
