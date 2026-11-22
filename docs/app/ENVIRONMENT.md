# Build environment: neuPod phone app

The machine the app is built and tested on, as checked at the start of phase 0. Update it when the toolchain changes.

Checked 2026-11-21 on the owner's Mac, over ssh in `tmux`.

| Part | Version | Notes |
| --- | --- | --- |
| Mac | Apple M1, 8 GB RAM, macOS 26.5.1 (25F80) | 8 GB is tight: run one emulator or simulator and one build at a time |
| Flutter | 3.50.1 stable (framework `ae9b586f34`, 2026-11-18), Dart 3.14.0 | Homebrew cask `flutter` |
| Xcode | 26.5 (17F42), command-line tools at `/Applications/Xcode.app/Contents/Developer` | First launch done |
| CocoaPods | 1.17.0 | Homebrew |
| iOS simulator | iOS 26.5 runtime (23F77): iPhone 17, 17 Pro, 17 Pro Max, 17e, Air | iOS 18.6 runtime (22G86) added on 2026-11-21 for testing the minimum, with the owner's approval |
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
- iOS CI runs on GitHub-hosted macOS runners.

## Goldens (screenshot tests)

The golden PNGs under `mobile/test/ui/{components,screens,device}/goldens` are made and compared on one machine only:
the `goldens` job of `.github/workflows/app.yml`, on the GitHub-hosted `macos-26` image (the label the `ios` job uses,
never `-latest`). Text rasterises differently on Linux and on each macOS release: after the build Mac moved from macOS
26.5 to 27.2.1, every component and screen golden made on 26.5 failed there by 0.00 to 0.21 % of its pixels, all on
glyph edges (2026-11-22). So a local Mac is not the reference.

- In CI the `goldens` job runs `flutter test test/ui/components test/ui/screens test/ui/device/goldens` with
  `NEUPOD_GOLDENS=compare` and fails on any mismatch. On failure it uploads the `golden-failures` artifact (each failed
  golden's master, test and diff images). It is one of the jobs `app-gate` waits for.
- The Linux `check` job runs the same tests without comparing pixels (overflow and exceptions still fail them).
- Locally, `flutter test` doesn't compare golden pixels and prints a note saying so. `NEUPOD_GOLDENS=compare flutter test
  ...` compares them, but a failure of about 0.1 % on glyph edges can be your Mac, not the app. `--update-goldens`
  writes nothing outside GitHub Actions.
- The job summary and the "Runner image" step log the image version (`ImageVersion`), which changes under the `macos-26`
  label when GitHub updates the image. If every golden suddenly fails by about 0.1 % on glyph edges with no app change,
  compare it with the run that made the PNGs and regenerate.

To regenerate (after an intended UI change, or an image update):

1. Push the branch, then run the workflow by hand with the input set:
   `gh workflow run app.yml --ref <branch> -f update_goldens=true` (or Actions > app > Run workflow, tick
   `update_goldens`). Only the `goldens` job runs, about 3 minutes. CI never pushes commits.
2. Download the `goldens` artifact: `gh run download <run-id> -n goldens -D /tmp/goldens`. It holds `components/goldens`,
   `screens/goldens` and `device/goldens`.
3. Copy it over the tree: `cp -R /tmp/goldens/. mobile/test/ui/`, check `git status` and look at every changed PNG
   (only the screens you meant to change should differ), then commit them (`test(app): regenerate goldens ...`) and push.
   The next PR run's `goldens` job compares against them.

Never commit `failures/` folders.

## Owner answers that set the targets (2026-11-21)

- Bundle and application ID: `net.stuartfraser.neupod` (spec Open question 4).
- Minimum OS: iOS 18, Android 10 (API 29) (Open question 5).
- Android test phone: the owner's Pixel; model and Android version still to be recorded (Open question 6).
