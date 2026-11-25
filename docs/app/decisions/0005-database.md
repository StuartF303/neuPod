# 0005: Library database

Status: Proposed (2026-11-21) · Owner: Architect

## Context

- Library of 20,000 tracks: artists, album artists, albums, discs, genres, compilations, embedded and folder art references.
- Named playlists and On-The-Go, with `.m3u8` import and export using paths relative to the library folder.
- Rating (0 to 5), play count, last played and skip count per track, in the app's own database, surviving a rescan, a retag
  and a file move within the library.
- Incremental rescan: size and mtime stored per file; a rescan with no changes in under ten seconds.
- Search as you type over artists, albums and songs, grouped by kind.
- Sort that matches the firmware's `lib_collate_key()` exactly (`app/library/lib_collate.c`): sort tag wins, a leading
  article skipped, ASCII case folded, Latin-1 and Latin Extended-A accents folded (ss, ae, oe, th, ij expand), then
  symbols (space first), digits, letters; key compared with `memcmp`, cut at 96 bytes.
- Every query off the UI isolate; the scanner writes in bulk without stalling the UI.
- Schema migrations that are generated or at least tested, since the database holds data the user cannot rebuild
  (ratings, playlists).
- Testable in pure Dart unit tests on a Linux CI host, without a device or emulator.
- Source-neutral track model (a track has a source and a location) so R2 (NAS) and R3 (device card) are additions.
- Licence compatible with MPL-2.0 distribution on the App Store and Google Play; actively maintained.

## Options considered

Facts from pub.dev and GitHub, checked 2026-11-21. Toolchain: Flutter 3.50.1, Dart 3.14.0.

| Package | Version (date) | Licence · publisher | Platforms | FTS | Off-UI isolate | Migrations | Host unit tests | Maintained |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **drift** + sqlite3 | 2.35.4 (2026-11-08); sqlite3 3.9.1 (2026-11-08) | MIT · simonbinder.eu (verified), Flutter Favorite | Android, iOS, desktop, web | FTS5 in the bundled SQLite on every platform | `NativeDatabase.createInBackground`, `computeWithDatabase` | `make-migrations`: schema snapshots, step-by-step code and generated tests | Yes, `NativeDatabase.memory()` in `dart test` | Yes: commits 2026-11-20 |
| sqflite | 2.4.6 (2026-11-10) | BSD-2-Clause · tekartik.com, Flutter Favorite | Android, iOS, macOS (desktop via `sqflite_common_ffi`) | Uses the OS SQLite: FTS5 not enabled in Android's framework SQLite | Platform channel / own thread | Hand-written `onUpgrade` | Via `sqflite_common_ffi` (a different engine path than production) | Yes: commits 2026-11-15 |
| ObjectBox | 5.3.2 (2026-05-20); 6.0.0-preview.6 (2026-11-03) | Apache-2.0 (binding; native core is a closed-source download) · objectbox.io | Android, iOS, desktop | No full-text search found; vector search only | Yes | Automatic model sync, UID-based | Needs the native library downloaded on the host | Yes: commits 2026-11-17 |
| isar | 3.1.0+1 (2023-04-25); 4.0.0-dev.14 (2023-08) | Apache-2.0 · isar.dev | all | Word index | Yes | Implicit | Native lib download | **No**: last commit 2025-06-14, no release since 2023, README says v4 "not ready for production" |
| isar_community | 3.4.1 (2026-10-13) | Apache-2.0 · isar-community.dev | Android, iOS, desktop | Word index (multi-entry) | Yes | Implicit | Native lib download; README says run tests with `-j 1` | Fork "focusing primarily on bug fixes and small updates for version 3"; small (163 likes) |
| realm | 20.2.0 (2025-09-24) | Apache-2.0 · realm.io | Android, iOS, desktop | Yes (RQL `TEXT`) | Yes | Schema version + callback | Native | **Deprecated**: README "We announced the deprecation of Atlas Device Sync + Realm SDKs in September 2024"; last commit 2025-10-23 |
| sembast | 3.10.1 (2026-11-09) | BSD-3-Clause · tekartik.com | all | No | Whole DB in memory, single isolate | Manual | Yes, pure Dart | Yes |
| hive / hive_ce | hive 2.2.3 (2022-06-30); hive_ce 2.24.0 (2026-11-19) | hive: unknown on pub.dev · hive_ce: Apache-2.0 / BSD-3 · iodesignteam.com | all | No | Boxes in memory | Manual adapters | Yes | hive: no; hive_ce: yes |

**drift.** A typed layer over SQLite with reactive queries (`watch()`), which suits lists that update while a scan
runs. Since `sqlite3` 3.x the SQLite library is supplied by a Dart build hook: prebuilt from upstream sources in the
package's GitHub releases, checked by sha256, compiled with `SQLITE_ENABLE_FTS5` among other options, and the same binary
family on Android, iOS and Linux, so production and CI run the same engine. `sqlite3_flutter_libs` is now
`0.6.0+eol` ("Not used anymore, update to version 3.x of package:sqlite3"), and the drift docs say depending on it is no
longer needed. FTS5 tables are declared in a `.drift` file with the `fts5` module enabled in `build.yaml`. It needs
`build_runner` codegen (`drift_dev`). We use that codegen because it gives compile-checked SQL and the migration
tooling. Plain SQL is available when the query builder gets in the way. The maintainer is one very active person
(Simon Binder), who also maintains `sqlite3`.

**sqflite.** Mature and simple, but it uses the operating system's SQLite. Android's framework SQLite does not enable FTS5
(Room users get `no such module: FTS5` and are told to bundle SQLite). FTS4 would mean different SQL on each platform,
and host tests would go through `sqflite_common_ffi` instead of the production path. It has no typed queries and no
migration tooling.

**ObjectBox, isar_community.** Fast object stores, but neither has SQL-style joins and grouping for the
artist/album/genre browse, nor a tested migration story for data the user cannot rebuild. ObjectBox has no full-text
search, and its core is a binary download. isar is abandoned; `isar_community` is a small maintenance fork of v3 and
too thin a base for a database we keep for years. **Realm** is deprecated. **sembast** and **hive_ce** keep data in
memory and have no FTS or ordered secondary indexes, which rules them out at 20,000 tracks.

## Decision

**Use drift 2.35 on `sqlite3` 3.9 (bundled SQLite through build hooks), with FTS5, opened in a background isolate.**
Do not depend on `sqlite3_flutter_libs`. Use `drift_flutter` only if its 0.3.x release has dropped the `+eol`
dependencies by the time of the spike; otherwise open `NativeDatabase.createInBackground(file)` with `path_provider` for
the path. It is the only option that meets every requirement: relational browse queries, FTS5 identical on both
platforms, a background isolate, generated and tested migrations, and in-memory host tests running the production
engine.

**Shape of the schema** (the library engineer owns the details):

- `sources(id, kind [local_folder | nas | device], uri_or_bookmark, display_name)`. R1 creates only `local_folder`.
- `tracks(id, source_id, rel_path, size, mtime, track_key, title, title_sort_key BLOB, artist_id, album_id, genre_id,
  disc_no, track_no, year, duration_ms, codec, sample_rate, bit_depth, playable_code, art_ref)`, unique on
  `(source_id, rel_path)`. Every track has a source and a location, so a NAS or device track is just another row.
- `artists`, `albums` (with album artist, year, `is_compilation`), `genres`. Each has `name`, `sort_name` (the sort tag,
  if any) and `sort_key BLOB` (indexed).
- `track_stats(track_key PRIMARY KEY, rating, play_count, skip_count, last_played)`. Keyed by content, not by row or
  path, so it is never deleted when tracks are removed. "Reset ratings and play counts" is the only thing that clears it.
- `playlists(id, name, kind [otg | named], position)` and `playlist_entries(playlist_id, position, track_key,
  rel_path_hint)`.
- `search_fts` as an FTS5 table (`kind, name, ref_id`, `tokenize='unicode61 remove_diacritics 2'`, `prefix='1 2 3'`),
  rebuilt by the scanner. A query is prefix terms (`"beat"*`) grouped by `kind`.

**Collation.** Port `lib_collate_key()` byte for byte to a pure-Dart function in the domain layer (`collateKey(name,
sort) -> Uint8List`, cut at 96 bytes), and store its output as an indexed `BLOB`. SQLite compares BLOBs with `memcmp`,
and a shorter key that is a prefix sorts first, which is exactly `lib_collate_cmp()`. So `ORDER BY sort_key` gives the
firmware's order with no custom collation registered per connection, and the fast-scroll letter is
`lib_collate_bucket()` applied to the first byte. Unit tests reuse the firmware's cases in `tests/test_library.c`
(`collate_case_and_the`, `collate_diacritics`, `collate_classes`, `collate_sort_tag_wins`). Ties break as the firmware
does in `app/library/lib_build.c` (full key, then id).

**Stable track key (`track_key`).** A content fingerprint of the audio, not of the tags or the path, so it survives a
retag, a rename and a move.

- `track_key = "a1:" + hex(SHA-256(codec ‖ audio_payload_length ‖ first 64 KiB of audio payload ‖ last 64 KiB of audio
  payload))`. The audio payload is defined per container so that metadata never enters the hash. FLAC: the frames after
  the last metadata block. MP3: the frames between ID3v2 and any trailing ID3v1/APE/Lyrics3 tag. MP4/M4A/ALAC: the
  `mdat` contents. WAV: the `data` chunk. Ogg/Opus: the packet bodies of the audio pages, not page headers, because a
  longer comment header renumbers pages.
- **FLAC fast path:** where STREAMINFO carries a non-zero MD5 of the decoded audio, use
  `"f1:" + md5 + total_samples + sample_rate` with no extra read. The firmware could compute this key cheaply too,
  which matters for R3 rating exchange.
- Cost: about 128 KiB read per new or changed file, well inside the five-minute first-scan target. Unchanged files
  (same size and mtime) are not read at all.
- **Move detection:** a rescan diffs the walk against `tracks` by `(source_id, rel_path)`. Paths that have disappeared
  are held as "missing" until the walk ends. Each new path gets a `track_key`. A new path whose key equals a missing
  row's key is a move, and that row's location is updated in place, keeping its id, queue references and playlist
  entries. Rows still missing at the end are deleted. Their `track_stats` stay, so a folder removed and added back gets
  its ratings back.
- Rejected: path (breaks on a move); whole-file hash (breaks on a retag, and reading 150 GB is far too slow); tags plus
  duration (breaks on a retag, and collides for untagged files).
- Known limits: byte-identical duplicates share one stats row (acceptable, and shown to be so in tests). A re-encode or
  re-rip is a new track. A one-to-one tag-plus-duration match within a single scan could carry stats over, but it is
  left out of R1 unless the owner asks for it.

## Consequences and risks

- **Build-time download.** The `sqlite3` hook downloads its prebuilt library from GitHub releases during `flutter build`
  and `dart test`. The app still makes no network connection at run time, but CI needs GitHub reachable, and builds
  depend on those release assets staying up. Fallbacks the hook supports: `url_pattern` to a mirror, or `source: source`
  to compile the SQLite amalgamation (public domain) ourselves. Escalate only if the owner objects to build-time
  downloads.
- Codegen: `build_runner` for drift. Generated files are committed and CI checks they are current.
- Single maintainer for drift and sqlite3. Both are very active, MIT, and SQL is portable, so a forced move to raw
  `sqlite3` would be survivable.
- Our own copy of SQLite adds about 1 to 2 MB per architecture to the app.
- The key algorithm becomes a compatibility contract (it keys the user's ratings). It carries a version prefix (`a1:`,
  `f1:`), and changing it needs a migration that recomputes keys before stats are re-joined.
- Writes from the scanner isolate must go through `computeWithDatabase` (or the same drift instance) so `watch()`
  streams in the UI see them.
- **Spec drift to raise with the lead:** `docs/ui.md` says `"A "` and `"An "` are not ignored yet, but
  `lib_collate.c` already skips them. The app follows the code. The doc needs correcting by whoever owns firmware docs.

## To verify in the phase 1 spike

1. `SELECT sqlite_version()` and `PRAGMA compile_options` on an iOS simulator, an iOS device, an Android emulator
   (arm64 and x86_64) and Linux CI. `CREATE VIRTUAL TABLE ... USING fts5` succeeds on all of them.
2. `drift_flutter` 0.3.1 with its `+eol` dependencies: does it pull a second SQLite or conflict with the hook? Decide
   between `drift_flutter` and plain `NativeDatabase.createInBackground`.
3. App size delta and a clean release build on both platforms with no extra native configuration.
4. `dart test` with `NativeDatabase.memory()` on GitHub's Linux runner. Measure the hook download time and whether it
   is cached.
5. 20,000 generated tracks: bulk insert time through `computeWithDatabase`, Songs page query under 16 ms, FTS prefix
   query under 50 ms, and UI frame times during a scan.
6. `track_key` stability: retag a FLAC, MP3, M4A and Ogg/Opus fixture (including growing the tag past its padding),
   rename and move them, and check the key is unchanged. Re-encode one and check the key changes.
7. Are size and mtime reliable through Android SAF (`DocumentFile.lastModified`) and iOS security-scoped bookmarks?
   If not, choose another change signal.
8. `make-migrations` round trip: v1 to v2 with a generated test, run in CI.

## Implementation (phase 1, 2026-11-22)

Built in `mobile/lib/data/db/` (database), `mobile/lib/data/scan/` (scanner, tag reader) and
`mobile/lib/domain/library/` (track model, collation, ports). Where it differs from the decision above, and why:

- **drift 2.35.4 and sqlite3 3.9.1 as decided, without `drift_flutter` and without `path_provider`.** `drift_flutter`
  0.3.1 still depends on the `+eol` `sqlite3_flutter_libs` and `sqlcipher_flutter_libs`, and `path_provider` 2.1 now
  pulls `jni`, `jni_flutter`, `objective_c` and `ffi` into the app for one directory name. The database is opened with
  `NativeDatabase.createInBackground` behind a `LazyDatabase`, in the directory that `neupod_storage.appSupportPath()`
  returns: Android `noBackupFilesDir`, iOS `Application Support/library` marked excluded from backup, never shown in
  Files or Finder. Excluded from backups because version 1 holds only what a rescan rebuilds; phase 3 revisits this
  when ratings and play counts arrive (they should survive a phone restore).
- **Phase 1 schema (version 1)** is the subset the vertical slice needs: `sources` and `tracks`, with `artist` and
  `album` as text on the track, the title sort tag in `title_sort`, `title_sort_key` as an indexed BLOB
  (`ORDER BY title_sort_key, id`), `reason_code` (an `UnreadableReason` code, never English text) and `scan_version`
  per row (the tag reader and sort key the row was read with: a row from another version is read again on the next
  scan, once, whatever its size and mtime, and an unavailable folder or a partial walk does not keep re-reading the
  rest). The schema snapshot is committed in `mobile/drift_schemas/library/drift_schema_v1.json`. Migrations are
  stepwise from version 1, tested against the snapshots, and a downgrade is refused (decision 0008). The
  `artists`/`albums`/`genres` tables, `track_key`, `track_stats`, playlists and FTS5 come with their phase 3 and 6
  items, as schema version 2 with a `make-migrations` test. Version 1 holds nothing the user cannot rebuild by
  rescanning.
- **Collation** is ported exactly as decided (`collateKey`, `compareKeys`, `collateBucket` in
  `domain/library/collation.dart`). Besides the cases of `tests/test_library.c`, a golden table generated from the C
  source itself (`scripts/app/gen-collate-golden.sh`, 1,176 names: every Latin-1 and Latin Extended-A/B code point,
  articles, symbols, other scripts, long names) is checked byte for byte.
- **Tag reading (decision 0003), phase 1 subset:** FLAC (STREAMINFO, Vorbis comments) and MP3 (ID3v2.2 to 2.4 text
  frames, Xing/Info with the LAME delay and padding, VBRI, else an estimate), title, artist, album, track number and
  duration, ported from `app/audio/meta.c`. Other formats are listed by file name until phase 3.

## Sources

All checked 2026-11-21.

- drift on pub.dev: <https://pub.dev/packages/drift>; repo <https://github.com/simolus3/drift>
- drift platforms (bundled SQLite, `sqlite3_flutter_libs` not needed): <https://drift.simonbinder.eu/platforms/>
- drift isolates: <https://drift.simonbinder.eu/isolates/>
- drift migrations (`make-migrations`): <https://drift.simonbinder.eu/migrations/>
- drift testing (`NativeDatabase.memory`): <https://drift.simonbinder.eu/testing/>
- drift FTS5: <https://drift.simonbinder.eu/sql_api/extensions/>
- drift_flutter: <https://pub.dev/packages/drift_flutter>
- sqlite3 on pub.dev: <https://pub.dev/packages/sqlite3>; hook options and compile flags
  <https://github.com/simolus3/sqlite3.dart/blob/main/sqlite3/doc/hook.md>; changelog
  <https://github.com/simolus3/sqlite3.dart/blob/main/sqlite3/CHANGELOG.md>
- sqlite3_flutter_libs (0.6.0+eol): <https://pub.dev/packages/sqlite3_flutter_libs>
- sqflite: <https://pub.dev/packages/sqflite>; Android FTS5 absence
  <https://slack-chats.kotlinlang.org/t/34195444/room-3-with-fts5>,
  <https://www.b4x.com/android/forum/threads/sqlite3-fts5.110369/>
- ObjectBox: <https://pub.dev/packages/objectbox>, <https://github.com/objectbox/objectbox-dart>
- isar: <https://pub.dev/packages/isar>, <https://github.com/isar/isar>; isar_community
  <https://pub.dev/packages/isar_community>, <https://github.com/isar-community/isar-community>
- realm: <https://pub.dev/packages/realm>, <https://github.com/realm/realm-dart>
- sembast: <https://pub.dev/packages/sembast>; hive <https://pub.dev/packages/hive>; hive_ce
  <https://pub.dev/packages/hive_ce>
- SQLite FTS5 (unicode61, `remove_diacritics`, prefix indexes): <https://sqlite.org/fts5.html>; BLOB comparison by
  `memcmp`: <https://sqlite.org/datatype3.html#comparisons>; SQLite licence <https://sqlite.org/copyright.html>
- Firmware collation: `app/library/lib_collate.c`, `app/include/library.h`, `tests/test_library.c`,
  `docs/adr/0017-music-library-index.md`
