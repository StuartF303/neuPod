# neuPod app (`mobile/`)

The neuPod phone app: a local music player for iOS and Android, in Flutter. The specification is
[docs/app/README.md](../docs/app/README.md); the package choices are in [docs/app/decisions/](../docs/app/decisions/).
Licence MPL-2.0 (see [LICENSE.md](../LICENSE.md)).

This file is for contributors. The user-facing build and install steps come in phase 7.

## Targets

| | |
| --- | --- |
| Application / bundle ID | `net.stuartfraser.neupod` |
| Display name | neuPod |
| Devices | Phones only (iPhone, `TARGETED_DEVICE_FAMILY = 1`), portrait only on both platforms |
| iOS | 18.0 minimum (Xcode project). iOS dependencies use Swift Package Manager, Flutter's default, so there is no Podfile; if a plugin ever makes Flutter generate one, set `platform :ios, '18.0'` in it. |
| Android | minSdk 29 (Android 10), compileSdk and targetSdk 36, Java 17 |

## Build and test

From `mobile/`:

    flutter pub get
    dart format lib test
    flutter analyze
    flutter test
    flutter build apk --debug
    flutter build ios --simulator --debug --no-codesign

## Layout

The layers of decision [0006](../docs/app/decisions/0006-state-management.md), each testable without the ones above it:

    lib/
      main.dart        runApp(ProviderScope(child: NeuPodApp()))
      app/             composition root: NeuPodApp, theme, router; providers.dart builds data and audio for the UI
      domain/          pure Dart: the library model, queue rules, collation, playlists, ports (interfaces)
      data/            implements domain ports: database, scanner, tag reader, art cache, folder access
      audio/           implements the Player port: playback, media session, interruptions, output route
      ui/              style, components, features/<feature>/<feature>_screen.dart and _providers.dart
    test/              mirrors lib/; architecture_test.dart enforces the dependency rule

Dependency rule: `domain` imports nothing from this app, no Flutter and no Riverpod. `data` and `audio` import `domain`
only. `ui` imports `domain` and `app/`, never `data` or `audio`. Riverpod appears only in `app/`, `ui/` and
`main.dart`. Imports inside `lib/` are `package:neupod/...` only. `test/architecture_test.dart` fails on a violation.

Planned in-repo plugins, added by their own items (not yet present):

- `packages/neupod_audio`: the thin audio engine plugin ([0001](../docs/app/decisions/0001-audio-engine.md)), used by
  `audio/`.
- `packages/neupod_storage`: folder access, SAF and security-scoped bookmarks
  ([0004](../docs/app/decisions/0004-folder-access.md)), used by `data/`.

Each will be a path dependency in `pubspec.yaml`, under MPL-2.0 like the app.

## Riverpod rules (decision 0006)

- Use `Provider`, `FutureProvider`, `StreamProvider`, `NotifierProvider` and `AsyncNotifierProvider`, with `.family`
  and `.autoDispose` where needed. No `package:riverpod/legacy.dart`, no `StateNotifier`, `StateProvider` or
  `ChangeNotifierProvider`, no `riverpod_generator`, no `hooks_riverpod`.
- `ref.watch` in `build`, `ref.read` in callbacks, `ref.listen` for one-shot effects.
- Business rules stay in domain or data, never in a Notifier.
- Tests: `ProviderContainer.test(overrides: [...])` for Notifiers; widget tests wrap screens in
  `ProviderScope(retry: (_, _) => null, overrides: [...])`.

## Rules

- Every Dart file starts with `// SPDX-License-Identifier: MPL-2.0`.
- R1 makes no network connection: no analytics, crash reporting, advertising or network packages. The release Android
  manifest has no `INTERNET` permission (Flutter adds it only to the debug and profile manifests, for the tool).
- Never commit signing keys, keystores, `key.properties`, provisioning profiles, certificates, `ExportOptions.plist`,
  `.env` files (`.env.example` is allowed) or music. The ignore rules are in the root `.gitignore` and
  `mobile/.gitignore`.
- **Fixture rule.** Every audio file under `mobile/` (`.flac`, `.mp3`, `.m4a`, `.wav`, `.ogg`, `.dsf` and the rest, in
  any letter case) is ignored, except in these fixture folders:
  `mobile/test/assets/`, `mobile/test/fixtures/`, `mobile/integration_test/assets/`,
  `mobile/packages/*/test/assets/` and `mobile/packages/*/example/assets/`. Put only generated or freely licensed
  test audio there, never anyone's own music. Keep personal test music in `mobile/test_music/`, which is always
  ignored. The firmware fixtures in `tests/assets/audio/` can be reused from there without copying.
- Every package that ships in the app gets a row in [LICENSE.md](../LICENSE.md).
