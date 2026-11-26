# 0013: The scale test and its targets

Status: Accepted (2026-11-26) · Owner: Test engineer · The targets marked "lead call" are proposals for the lead and the
owner; the spec's own are marked "spec"

## Context

- The spec (The library): "Sizing target: 20,000 tracks. First scan of 5,000 FLAC files in under five minutes on a
  mid-range phone; a rescan with no changes in under ten seconds. (Targets are proposals; measure and report.)" Its
  test table: "A generated 20,000-track library: scan time, memory, scroll smoothness", at the end of the library
  phase. It sets no target for list reads, the database or memory.
- The last handoff asked for "real queries" before the scale test: `DriftLibraryView` reads the whole tracks table and
  folds it into the firmware's entities in Dart.
- `mobile/tool/gen_test_library` already wrote a scale library, but not a real library's shape (200 artists of 10
  albums of 10 tracks, FLAC and MP3 only, no compilations).

## Decision

**The scale library** (`scaleCases`, `--scale N`) is shaped like a real one: about 13 tracks an album and 2.5 albums
an artist, so 20,000 tracks are 1,500 album folders by 563 album artists; FLAC, MP3 and M4A (ALAC) in turn, one format
an album, with real tags written into the repository's small fixtures, so the real tag readers run; a compilation
every 15th album (by "Various Artists" or by the flag alone, its tracks by other artists); two discs every 8th other
album (disc tags, `(Disc n)` folders); "The" artists, some in ", The" folders; accented, Nordic, Cyrillic and Japanese
names; every 50th track untagged (owner decision 15, so each makes its folder an album: 1,901 albums in the Albums
list). 20,000 tracks are 314 MB. The generator gained an M4A tag writer (`m4aWithTags`), which also fixed the curated
library's "untagged" ALAC file: it was the fixture byte for byte, and the fixture has tags.

**The test** is `mobile/test/scale/library_scale_test.dart` (tag `scale`). It generates the library in a temporary
folder, scans it with the app's `LocalLibraryScanner` (through a dart:io storage root, `test/support/host_library.dart`)
into a database file opened as the app opens it (SQLite in a background isolate), and deletes it all afterwards.

- **Every PR** runs it at 2,000 tracks inside `flutter test`, against the limits below. At that size the limits are
  loose, so they catch a regression of several times, not a slow percent.
- **The full run** is `NEUPOD_SCALE_TRACKS=20000 flutter test test/scale` from `mobile/`, or the app workflow's
  `scale_tracks` input (`gh workflow run app.yml --ref <branch> -f scale_tracks=20000`), which uploads the numbers as
  the `scale-report` artifact. `NEUPOD_SCALE_REPORT=<file>` writes them as JSON locally.

### Targets and limits

| What | Target | Source | Limit in the test (host) |
| --- | --- | --- | --- |
| First scan | 5,000 FLAC files in 5 minutes on a mid-range phone (60 ms a file) | spec | 60 ms × tracks |
| Rescan, nothing changed | under 10 s at 20,000 | spec | 10 s |
| Rescan, 100 changes (60 retagged, 20 removed, 20 added) | under 16 s at 20,000 (the unchanged rescan plus 100 files at the first scan's rate) | lead call | 16 s |
| First list after a launch or a scan (the table read and folded) | under 1 s at 20,000 | lead call | 1 s |
| Any list once loaded: Artists, Albums, Cover Flow, Songs, an artist's albums, an album's tracks | under 100 ms at 20,000 | lead call | 100 ms |
| A Search keystroke (the prefix match over every name) | under 50 ms at 20,000 | lead call | 50 ms |
| Database | under 2 KB a track (40 MB at 20,000) | lead call | 2 KB × tracks |
| Memory | the library view's lists under 50 MB at 20,000 | lead call | reported, not enforced (RSS is noisy across runners) |
| Scroll smoothness | no dropped frames scrolling Songs at 20,000 | spec (test table) | the device check: the host cannot measure frames |

### Numbers, host (2026-11-26)

Apple M1 Mac, 8 GB, macOS 27.2.1, Flutter 3.50.1, `flutter test` (debug, JIT). Host times are the code's cost, with a
fast SSD and a desktop CPU; they say little about a phone's storage.

| | 2,000 tracks | 20,000 tracks |
| --- | --- | --- |
| Library generated | 31 MB | 314 MB, 1,500 album folders |
| First scan | 1.9 s | 17.6 to 18.5 s (0.9 ms a file) |
| Rescan, nothing changed | 0.1 s | 0.8 to 1.1 s |
| Rescan, 100 changes | 0.16 s | 0.8 to 0.9 s |
| Database (after a checkpoint) | 1.7 MB | 17.2 MB (860 bytes a track) |
| Table read alone (all rows, from the database isolate) | 12 ms | 188 to 193 ms |
| First list after a scan (read and fold) | 54 ms | 424 to 428 ms |
| Artists / Albums / Cover Flow, loaded | 0.05 ms / 0.04 ms / 0.02 ms | 0.07 ms / 0.15 ms / 0.16 ms |
| Songs, loaded | 0.09 ms | 1.2 to 5.2 ms |
| An artist's albums / an album's tracks | 0.18 ms / 1.1 ms | 0.17 ms / 0.9 ms |
| Search: building its index / one keystroke | 16 ms / 0.6 ms | 29 to 37 ms / 3.3 ms |
| Lists counted | 59 artists, 191 albums, 10 compilations | 563 artists, 1,901 albums, 100 compilations |
| Process memory (max RSS) | 259 MB | 387 to 408 MB (231 MB at the start; the lists add 5 MB) |

Every target is met on the host. The phone numbers come from the device check (PROGRESS, phase 3).

### "Real queries": not now (lead call)

`DriftLibraryView` does not query each list. It reads the tracks table once (one `SELECT` joined to its source) and
folds it into the firmware's artists, albums and genres in Dart (`lib_build.c`'s `index_file`, `stage_entity`,
`ent_cmp`, the walk order), then keeps that until the tracks or sources change. Measured at 20,000:

- every list once loaded takes 0.02 to 5 ms, 20 to 5,000 times under the 100 ms bar, and no index would help it (no
  list is a query);
- the one slow step is the first list after a launch or a scan: 0.43 s, of which 0.19 s is the table read and the rest
  the fold. It happens once, and in practice before the user opens a list: Home counts the songs at launch and again
  when a scan ends, which loads the view.

Per-list SQL would need the firmware's entity keys stored at scan time (the collation keys of the album artist and the
album, folded as `ent_cmp` folds them, and the walk order), a schema version 6 with a scan version bump, so every
owner's library is read again once, and SQL that reproduces the firmware's fold exactly. That is the bigger, riskier
change, for a cost the user should not see. **So the fold stays**, and the stored-keys design is the plan if the device
check shows the first list over 1 s on the iPhone or the Pixel. No schema change was made, so decision 0008's
migration rules and the guard tests are untouched.

## Alternatives

- **Measure on a device only.** It is the real number, but cannot run on every PR; the host run catches regressions
  and the device check gives the phone's number.
- **A fixed set of committed scale files.** 314 MB in a public repository, and no way to change the shape; generated
  at test time instead, never committed.
- **Every limit scaled with the size.** A list read must stay quick at any size, so only the first scan's limit grows
  with the track count.
