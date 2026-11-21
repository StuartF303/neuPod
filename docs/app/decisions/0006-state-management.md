# 0006: State management and dependency wiring

Status: Proposed (2026-11-21) · Owner: Architect

## Context

- Layers, each testable without the ones above it: pure-Dart **domain**, then **data** (database, scanner, tag reader,
  art cache), then **audio** (player, media session), then **UI**.
- A long-running background scan with progress, cancel, and resume after the app is closed.
- A player state stream (track, position, playing, queue, shuffle and repeat, output route) feeding Now Playing, the
  mini-player and the lock screen.
- Reactive library lists (drift `watch()` streams, see [0005](0005-database.md)) that update while a scan runs.
- Golden and widget tests for every screen in four style/mode combinations, at two sizes and the largest text size,
  using fakes and no platform plugins.
- Built mostly by AI coding agents: we want a boring, well-documented approach with one idiomatic way to do each thing,
  and no heavy codegen unless it pays for itself.
- MPL-2.0-compatible licence, actively maintained.

## Options considered

Facts from pub.dev and GitHub, checked 2026-11-21. Toolchain: Flutter 3.50.1, Dart 3.14.0.

| Package | Version (date) | Licence · publisher | Last commit | Layering | Scan / player streams | Test fakes | Codegen | Agent fit |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **flutter_riverpod** / riverpod | 3.5.0 (2026-11-06) | MIT · dash-overflow.net (riverpod is a Flutter Favorite) | 2026-11-17 | Providers wrap plain Dart services; lower layers never import it | `StreamProvider`, `AsyncNotifier` | `ProviderScope(overrides:)`, `ProviderContainer.test`, `overrideWithValue`, `overrideWithBuild` | Optional (`riverpod_generator` 4.0.10) | Large docs. Risk: agents write 2.x idioms |
| flutter_bloc / bloc | flutter_bloc 9.1.1 (2025-05-02); bloc 9.2.1 (2026-05-12) | MIT · bloclibrary.dev, Flutter Favorite | 2026-11-19 | Blocs and Cubits are plain Dart; DI through `RepositoryProvider` | Cubit subscribes to the stream | `bloc_test` 10.0.0, `MockBloc` | None | Very stable API since 8.x. More boilerplate (state classes, events) |
| provider | 6.1.5+1 (2025-08-19) | MIT · dash-overflow.net, Flutter Favorite | 2026-09-29 | `InheritedWidget` sugar | `StreamProvider`, `ChangeNotifier` | Wrap in providers | None | Stable, but its author points new work to Riverpod. Weak async and disposal story |
| signals / signals_flutter | 7.1.0 (2026-05-29) | Apache-2.0 · rodydavis.com | 2026-11-06 | Fine-grained reactive values | Stream to signal adapters | Manual | None | Small user base (26k downloads in 30 days), one maintainer, few examples |
| ChangeNotifier / ValueNotifier + get_it (+ injectable, watch_it) | get_it 9.3.0 (2026-09-19); injectable 3.0.0 (2026-04-20); watch_it 2.5.0 (2026-09-19) | MIT · flutter-it.dev, codeness.ly | 2026-09-29 | Flutter SDK types; a global locator | Hand-written `StreamSubscription` | `GetIt.I.reset()` and re-register per test | injectable is codegen | Simple, but a global locator leaks state between parallel tests, and loading/error/dispose are hand-rolled each time |

**Riverpod 3.** Major version 3.0 (2025) merged the AutoDispose and Family variants into a single `Notifier` and a
single `Ref`. It added `ProviderContainer.test`, `overrideWithBuild`, `WidgetTester.container` and automatic retry of
failed providers, and moved `StateNotifierProvider`, `StateProvider` and `ChangeNotifierProvider` to
`package:riverpod/legacy.dart`. Every docs page shows a hand-written form next to the generator form, so codegen is not
required. Overrides are the cleanest way to swap a fake `LibraryRepository` or `Player` into one golden test without a
global locator. `StreamProvider` maps directly onto drift `watch()` and the player state stream. The main risk is that
agents trained on Riverpod 2 will reach for `StateNotifier`, `AutoDisposeNotifier` or `ref.watch` inside callbacks.

**bloc.** The most explicit and stable option, with excellent docs and `bloc_test`. It costs one state class per screen,
and usually an event class too. It also puts a second DI mechanism (`RepositoryProvider`, built on provider) next to
the blocs. A good choice, but more ceremony than this app needs, and dependency overrides in widget tests are clumsier
than Riverpod's.

**ChangeNotifier + get_it.** Least to learn, but every async load, error state and disposal is hand-written. The
global locator has to be reset carefully for parallel widget tests, and `injectable`'s codegen does not pay for itself
in an app with about a dozen services.

## Decision

**Use `flutter_riverpod` 3.5 without code generation, and only in the UI and the composition root.** Domain, data and
audio do not import Riverpod: they expose plain Dart classes, `Stream`s and `Future`s behind interfaces declared in
domain. Providers live in one place per feature on the UI side and do three jobs: build the real service (or a fake,
via override), adapt streams (`StreamProvider`), and hold UI-facing state (`Notifier` / `AsyncNotifier`).

Rules for agents (to go into `mobile/README.md` for contributors and be enforced in review):

- Use `Provider`, `FutureProvider`, `StreamProvider`, `NotifierProvider` and `AsyncNotifierProvider`, with `.family`
  and `.autoDispose` where needed. No `package:riverpod/legacy.dart`, no `StateNotifier`, `StateProvider` or
  `ChangeNotifierProvider`, no `riverpod_generator`, no `hooks_riverpod`.
- `ref.watch` in `build`, `ref.read` in callbacks, `ref.listen` for one-shot effects (panels).
- Business rules stay in domain or data, never in a Notifier. A Notifier forwards calls and maps state.
- The scan is a data-layer `LibraryScanner` with `Stream<ScanProgress>` and `cancel()`. The UI reads it through a
  `StreamProvider` and calls `cancel` through `ref.read`. Running when the app is closed (WorkManager / BGTask) is a
  data/platform concern, not a Riverpod one.
- The player is an audio-layer `Player` interface exposing `Stream<PlayerState>`. A single `playerStateProvider`
  (`StreamProvider`) feeds every screen.
- Tests: `ProviderContainer.test(overrides: [...])` for Notifiers. Widget and golden tests wrap screens in
  `ProviderScope(overrides: [libraryRepositoryProvider.overrideWithValue(FakeLibraryRepository(...)), ...])`, with
  retry disabled so failures surface at once.
- Add `riverpod_lint` 3.2.0 (MIT) if it runs cleanly with the analyzer that Flutter 3.50 uses. Otherwise review
  enforces the rules above.

Why: it keeps the lower layers framework-free and so testable in plain `dart test`. Overrides make the golden matrix
cheap. Drift and player streams plug in without glue code. There is no codegen beyond drift's.

## Consequences and risks

- **Agent drift to Riverpod 2 APIs** is the main risk. Mitigations: the rule list above, the lint, and the reviewer
  checking imports. If it keeps happening after phase 1, fall back to `flutter_bloc` Cubits, which this layering allows
  without touching domain, data or audio.
- Riverpod has had a breaking major (2 to 3) within the last two years, so pin `^3.5.0` and treat a 4.0 as a planned
  migration.
- Automatic retry (on by default in 3.x) can hide a failing database open behind repeated retries. Set the
  `ProviderScope` retry to none for providers that open resources, and in all tests.
- One maintainer (Remi Rousselet), very active. MIT licence.

## To verify in the phase 1 spike

1. `ProviderScope` overrides plus a golden test of a list screen fed by a `FakeLibraryRepository`, running headless on
   Linux CI.
2. Scan progress through a `StreamProvider` while drift `watch()` lists update, with no dropped frames at 20,000 tracks.
3. `playerStateProvider` driving Now Playing and surviving the app going to the background and back.
4. `riverpod_lint` 3.2.0 works with Flutter 3.50.1's analyzer (`dart analyze` clean, no plugin errors).
5. Retry disabled in tests: a provider that throws fails the test at once.

## Sources

All checked 2026-11-21.

- flutter_riverpod <https://pub.dev/packages/flutter_riverpod>, riverpod <https://pub.dev/packages/riverpod>, repo
  <https://github.com/rrousselGit/riverpod>
- Riverpod 3.0 changes (legacy providers, `ProviderContainer.test`, overrides, retry): <https://riverpod.dev/docs/whats_new>
- riverpod_generator <https://pub.dev/packages/riverpod_generator>; riverpod_lint <https://pub.dev/packages/riverpod_lint>
- flutter_bloc <https://pub.dev/packages/flutter_bloc>, bloc <https://pub.dev/packages/bloc>, bloc_test
  <https://pub.dev/packages/bloc_test>, repo <https://github.com/felangel/bloc>
- provider <https://pub.dev/packages/provider>, repo <https://github.com/rrousselGit/provider>
- signals <https://pub.dev/packages/signals>, signals_flutter <https://pub.dev/packages/signals_flutter>, repo
  <https://github.com/rodydavis/signals.dart>
- get_it <https://pub.dev/packages/get_it>, watch_it <https://pub.dev/packages/watch_it>, injectable
  <https://pub.dev/packages/injectable>, repo <https://github.com/flutter-it/get_it>
- mocktail (optional, for fakes) <https://pub.dev/packages/mocktail>

## Proposed layout of mobile/lib

```
mobile/
  lib/
    main.dart                 # runApp(ProviderScope(child: NeuPodApp()))
    app/                      # composition root: NeuPodApp, router, theme selection
      providers.dart          # infrastructure providers: database, scanner, player, clock, file access
    domain/                   # pure Dart, no Flutter imports (enforced by a test that scans imports)
      library/                # Track, Album, Artist, Genre, Source, TrackLocation, compilation rules
      collation/              # collateKey(), bucket() - port of lib_collate.c
      queue/                  # queue, shuffle, repeat, skip-back rule
      playlists/              # m3u8 parse and write
      ports/                  # interfaces: LibraryRepository, StatsRepository, Player, LibraryScanner, FolderAccess
    data/                     # implements domain ports; may import Flutter plugins, never UI
      db/                     # drift database, tables, .drift files (FTS5), migrations, generated code
      scan/                   # scanner isolate, tag reading and folder fallbacks, track_key
      art/                    # art cache
      platform/               # folder pickers, SAF and bookmarks
    audio/                    # implements Player; media session, interruptions, output route
    ui/
      style/                  # Classic/Modern tokens, skins, Montserrat, Lucide
      components/             # rows, headers, rail, panels
      features/
        home/  browse/  album/  now_playing/  queue/  search/  playlists/  settings/
          <feature>_screen.dart
          <feature>_providers.dart   # feature-level Notifiers and StreamProviders
  test/                       # mirrors lib/ one-to-one
    domain/ ...               # plain `dart test`-style unit tests
    data/ ...                 # drift NativeDatabase.memory(), scanner on generated fixture trees
    audio/ ...                # Player against fake platform channels
    ui/ ...                   # widget tests with ProviderScope overrides
    goldens/                  # per screen x {Classic, Modern} x {Light, Dark} x {small, large, max text}
    fakes/                    # FakeLibraryRepository, FakePlayer, FakeScanner, FakeFolderAccess (shared)
    drift/                    # migration tests generated by make-migrations
  integration_test/           # device/simulator flows: pick, scan, browse, play, background, resume
```

Dependency rule: `domain` imports nothing from this app. `data` and `audio` import `domain` only. `ui` imports `domain`,
and `app/providers.dart` imports everything. Riverpod appears only under `app/` and `ui/`. A unit test in
`test/architecture_test.dart` reads the import lines and fails on a violation.
