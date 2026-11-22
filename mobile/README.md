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

## Spec suites

`test/spec/` holds tests written from the specification, not from the code: the test engineer writes them before or
while an item is built, against small interfaces declared in each test file. Until an implementer wires them they are
skipped with the reason `awaiting implementation of <item>`, so `main` stays green. Each file carries `@Tags(['spec'])`
(the generator's own tests carry `tool`).

| File | Item | Interfaces to wire |
| --- | --- | --- |
| `collation_spec_test.dart` | Sort rule (owner decision 11), a port of `lib_collate_key()` | `CollationKey`, `CollationLetter` |
| `wheel_accel_spec_test.dart` | Wheel acceleration, a port of `ui_accel_feed()` | `WheelAccel` (`WheelAccelFactory`) |
| `wheel_touch_spec_test.dart` | Wheel hit zones and press-or-turn (device view sections 4 and 5) | `WheelGeometry`, `WheelTouch` |
| `library_rules_spec_test.dart` | Folder fallback, `, The`, discs, compilations, unplayable files | `FolderFallback`, `FolderNameDisplay`, `LibraryFromTracks` (`LibraryView`) |
| `scanner_spec_test.dart` | Read-only guarantee and the generated library's manifest | `LibraryFromFolder` |
| `wheel_text_entry_spec_test.dart` | Search typed with the wheel (owner decision 12): a port of the firmware's text entry, and the Search screen | `TextEntry` (`TextEntryFactory`, `TextEntryFromBytes`), `SearchScreen` |
| `text_tiers_spec_test.dart` | Screen scale and text tiers (device view sections 1 and 8) | `IosTier`, `AndroidTier`, `MetricsOf`, `ScreenScale` |

**Wiring a suite.** Each file has a `// WIRE:` block with `final <Interface>? wired<Name> = null;`. Replace `null` with
the app's function, or a small adapter written in that block from the app's types to the ones the test declares (the
interfaces are deliberately small so adapting is a few lines). The skip lifts by itself once every value a group needs
is set. Do not change the expectations to fit the code: if a test looks wrong, raise it with the test engineer and the
lead, since it is the specification that decides. Cases the specification leaves open are not asserted either way; the
generator's manifest marks them with `ambiguous` and a reason.

Run them: `flutter test test/spec` (or `flutter test --tags spec`). On this Mac run tests through
`~/bin/neupod-buildlock`.

## The generated test library

`tool/gen_test_library/` writes a library in the owner's convention, `Artist/Year Album/NN Title.ext`, built from the
repository's own fixtures (`tests/assets/audio` and `tests/assets/art`) with tags written by a small FLAC Vorbis comment
and ID3v2.3 / v2.4 writer. It is generated on demand and never committed. From `mobile/`:

    dart run tool/gen_test_library/main.dart --out build/test_library
    dart run tool/gen_test_library/main.dart --out build/scale_library --scale 20000

The curated library (about 60 files, 1.5 MB) covers: full FLAC and ID3 tags, `Folder.jpg` and `cover.jpg`, embedded
art, `, The` folders with and without tags, discs as `(Disc n)` in album tags and in folder names and as disc-number
tags over two folders, two albums of the same name, compilations by `Various Artists` and by the flag, very long folder
names and titles, accents (UTF-8 and UTF-16 tags), symbols and digits leading names, articles and a sort tag, missing
tags for the folder fallback, partial tags, unplayable files (empty, garbage, truncated), files at the root, and
hidden, AppleDouble and non-audio files. `manifest.json` beside `library/` lists every file, its case, the tags written
and what the app should show; `"ambiguous"` marks what the specification does not settle.

The scale library is N tracks (artists of ten albums of ten tracks, alternating FLAC and MP3, a small `Folder.jpg` per
album, every 50th track untagged, some two-disc albums and `, The` folders). 20,000 tracks take about 330 MB: write
them only under `build/` and delete them after the scale test. `--out` must be new, empty or an earlier output of the
tool (it holds a `.neupod-test-library` marker); the tool refuses any other folder, so it cannot delete real music.
`test/spec/gen_test_library_test.dart` checks the generator itself on every run.

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
