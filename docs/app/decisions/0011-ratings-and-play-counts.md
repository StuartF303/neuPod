# 0011: Ratings, play counts and last played

Status: Accepted (2026-11-25) · Owner: Library engineer · Owner decision 20 (spec, 25 November 2026) settled the three
open questions

## Context

- The spec (`README.md`, The library) asked for ratings, play counts, last played and skip counts per track in the
  app's own database, "keyed so that they survive a rescan and a file move within the library". Decision 0005 planned
  a `track_stats` table keyed by a content fingerprint for that.
- The firmware is the reference (constitution II). It keeps ratings in `app/settings/ratings_store.c` and plays in
  `app/settings/plays_store.c`, both **keyed by the file's path and size**: `ratings_key(path, size)`, an FNV-1a hash
  of the path mixed with the size (`ratings_store.c:26`; `plays_store.h`, "An entry is keyed like a rating"). A rebuilt
  library keeps them; a moved file, or a retag that changes the size, starts again at nothing.
- So the spec and the device disagreed on a move. The firmware has **no skip count** at all. Decision 0005 excluded
  the database from phone backups until ratings arrived. All three went to the owner (questions below, answered).

## Owner decision 20 (25 November 2026)

1. **Identity: the device's key, plus a carry-over.** Ratings and play counts are keyed by path and size, as on the
   device. When a rescan sees a file move (its old path gone, one new file with the same audio fingerprint), they are
   carried to the new key. (Options were: A, the device's key only; B, a content fingerprint as the key; C, the
   device's key with a carry-over. C was recommended and chosen.)
2. **Skip counts are dropped for now**, on the app and the device.
3. **Backups: ratings and play counts get their own small database, included in iCloud and Google backups**; the
   library database stays excluded. (Options were: keep everything excluded; back up the library database; a
   separate stats database. The last was recommended and chosen.)

## The firmware rules, ported

All in `mobile/lib/domain/library/play_stats.dart`, checked against the compiled C by
`test/domain/library/play_stats_vectors_test.dart` with `test/fixtures/plays/firmware_vectors.json`, which
`scripts/app/gen-plays-vectors.sh` prints from the firmware's own `play_tracker.c`, `ratings_store.c` and
`plays_store.c` (`scripts/app/plays_vectors.c`). CI regenerates the file and fails on a difference.

| Rule | Firmware | App |
| --- | --- | --- |
| A track start counts as a play once, when half its length has been heard, or 30 s when the length is unknown, or when it ended by itself after 3 s heard | `ptrk_update`, `app/runtime/play_tracker.c:12`; constants `app/include/play_tracker.h:23-25` | `PlayTracker.update` |
| Only time spent playing counts (a seek or scan does not move it); a held-up pass adds at most 1 s; a new track start begins again; times are `uint32_t` and wrap | same | same (masked to 32 bits) |
| A pass every 100 ms on the monitor task, whatever screen is up; playing = `PLAYER_PLAYING` and not held for a Bluetooth headset; ended = `PLAYER_ENDED`; a new `track_id` on every start, a skip and a gapless handoff, not on a seek | `count_plays`, `app/runtime/player.c:441`; `app/runtime/runtime.c:328`; `player.h:59` | `PlayCounter` (`domain/library/play_counter.dart`): a 100 ms timer while a track is current; playing = `PlayerState.isPlaying`; ended = `PlaybackStatus.ended`; a start = a new queue or another index |
| A rating is 0 to 5; 0 clears it; anything above 5 is ignored | `ratings_set`, `ratings_store.c:45` | `PlayStats.rated` |
| A counted play adds one (saturating at 65535, `uint16_t`) and stamps "last played" with the serial; any touch (a play, a note) counts the serial up and stamps the entry | `apply`, `plays_store.c:50-91` (saturation `:84`) | `PlayStats.touched`; the serial is the largest stamp, so it lives in the table |
| "Last played" is a serial, not a time: "the player has no real clock on every board" | `plays_store.h` | kept as a serial, so the order matches the device's |
| The wheel sets the stars one per turn whatever the turn's size, clockwise more, 0 to 5; a haptic step only when they change; saved at once | `p2_scroll`, `app/ui/ui_nowplaying.c:788-802` | `starsAfterTurn`; `NowPlayingScreen` page 2 |
| A rating of 4 stars or more notes the track (zero plays) so Top rated can open it | `ui_nowplaying.c:795` | `TrackStatsStore.rate` |

## Storage

- **Its own database: `stats.sqlite`** (`lib/data/db/stats_database.dart`, `StatsDatabase`), schema version 1, one
  table `track_stats(path, size, rating, plays, last_played, stamp)`, primary key `(path, size)`, CHECK `rating` 0 to 5
  and `plays` 0 to 65535. Migrations follow decision 0008 (snapshots in `drift_schemas/stats/`, one transaction, a
  downgrade refused; with one version any upgrade throws and changes nothing). Tests: `test/drift/stats/`. The guard
  test allows exactly `sources` and `tracks` in the library database and exactly `track_stats` in the stats database.
- **Where it lives (owner decision 20, backups):** neupod_storage `backedUpDataPath()`.
  - iOS: `Application Support/stats`, created if needed, with `isExcludedFromBackup` set to **false** and read back
    (the call fails if it is still excluded); the flag on a folder covers its contents. The library stays in
    `Application Support/library`, flag **true** (decision 0005).
  - Android: `filesDir/stats`. The manifest now names explicit rules: `res/xml/backup_rules.xml`
    (`android:fullBackupContent`, Android 11 and earlier) and `res/xml/data_extraction_rules.xml`
    (`android:dataExtractionRules`, Android 12 and later, cloud backup and device transfer), each with the one rule
    `<include domain="file" path="stats/"/>`. With an include rule nothing else is backed up; the library database
    and the art cache are in `noBackupFilesDir`, which Android never backs up anyway.
  - Checked by `test/app/backup_rules_test.dart` (the Android rules and manifest) and, on the iOS simulator and the
    Android emulator in CI, by `integration_test/backup_paths_test.dart` (iOS: the real flags of both folders and of
    `stats.sqlite`; Android: the paths).
- **The library database, schema version 5**, adds one column, `tracks.audio_fingerprint` (null until a rescan reads
  the file again: scan version 6). The never-merged first draft of this PR put `track_stats` in the library database
  as version 5; it was restructured before merge, so the chain on `main` goes 4 to 5 with only the new column.
- **Key: the device's identity.** `path` is the device's form of the track's path, `'/' + tracks.rel_path`, as a
  card's root gives it; `size` is the file's size. The app stores the two themselves, not the firmware's 32-bit hash,
  so two tracks never share numbers through a hash collision. The hash can be computed from them when R3 exchanges
  ratings with a device.
- **Nothing ties the table to `tracks`**; it is another database. A rescan, a re-read file, a new scan version, a
  removed folder or a deleted file never touches a row; a folder added back finds its numbers again.
- **One table for the firmware's two stores.** A row with `stamp` 0 is a rating the plays store would not know; a row
  with no rating and `stamp` 0 is deleted.
- **The app keeps every track; the device keeps 128 (24 ratings on the Pico 2 W) and evicts.** Memory lets a device do
  less, never differently (constitution IV), so the app does not port the eviction, the 96-byte path limit
  (`plays_store.c:101`) or the copy of title and artist (the app has the library).

## The carry-over on a move or a retag

- **The fingerprint** (`lib/data/scan/audio_fingerprint.dart`): `a1:` and the hex SHA-256 of the container family
  (`flac`, `mp3`, `mp4`, `wav`), the audio payload's length in bytes and the first 64 KiB of the payload. The payload
  is where the container keeps the audio, and nothing else:
  - FLAC: everything after the metadata block flagged last (an ID3v2 tag in front is skipped). Comments, pictures and
    padding are metadata blocks.
  - MP3: from the first frame header after any ID3v2 tags (and padding) to the end, less APEv2, Lyrics3v2 and ID3v1
    at the end.
  - MP4/M4A: the body of the first top-level `mdat` box. Taggers rewrite `moov`/`udta` and may move `mdat`, never its
    body.
  - WAV: the body of the `data` chunk (`LIST` and `id3 ` chunks are tags).
- **Why this one.** Cheap: one 64 KiB read per new or changed file, from the handle already open for its tags, in the
  scan's isolate; unchanged files are never opened. Robust: no tag byte is hashed, so a retag, a rename and a move
  leave it unchanged. The payload length stands in for the owner's "plus the duration": it is exact and needs no
  parse, while a duration can be an estimate (an MP3 with no Xing header) and comes out the same for different
  encodings; two recordings that share their first 64 KiB (a silent intro) still differ unless they are also exactly
  as long to the byte. A re-encode or a re-rip is new audio and starts again, as on the device.
- **A size-changing retag carries over: yes**, for FLAC, MP3, MP4/M4A and WAV, since the fingerprint has no tag bytes.
  Ogg, Opus and raw AAC files are listed by name and never opened by the scanner (the device does not parse them), so
  they have no fingerprint and no carry-over; their numbers stay under the old key.
- **The rule** (`LocalLibraryScanner`, `_Moves`): across every folder of one scan, a key that left (a file gone from a
  complete walk, a folder removed, or a file whose size changed) and a key that arrived (a new file, or the new size)
  with the same fingerprint, exactly one of each, is a move. At the end of the scan the store carries the numbers
  (`TrackStatsStore.carry`), in one transaction. Copies of the same audio on either side are not a move (which one
  would it be?). A new key that already has numbers keeps them; the old ones stay under the old key. A scan cancelled
  before the end carries nothing; the numbers stay under the old key, not lost.
- Tests: `test/data/scan/audio_fingerprint_test.dart` (each container, retagged and not, other audio, damaged input)
  and the "ratings and play counts" group of `test/data/scan/local_library_scanner_test.dart` (a move in a folder, a
  move to another library folder, a size-changing retag, copies, no overwrite).

## Known gaps (logged in PROGRESS)

- **A restart of the same queue entry is not a new start yet.** The engine does not report one: Previous after 3 s and
  (phase 4) repeat one restart the same index, so a second play in a row of the same entry is not counted. On the
  device they are new starts (`begin_track`, `player.c`). Needs a start counter from `neupod_audio` (audio engineer).
- Plays are counted by Dart. Where Android keeps playing after the Flutter engine has gone (the media service alone),
  nothing counts them until the app is back.
- The device's page 2 puts the stars above the rows, lit stars in the accent colour and the rest in the line grey.
  The app keeps owner decision 13's Rating row and its existing star skin (filled and outlined stars in the secondary
  and tertiary inks): a UI styles question for the owner.
- Most played, Top rated and Unplayed (Music > Playlists on the device) and Settings > Reset ratings and play counts
  belong to phase 6; the numbers they need are stored now.

## Sources

- Firmware: `app/runtime/play_tracker.c`, `app/include/play_tracker.h`, `app/settings/ratings_store.c`,
  `app/include/ratings_store.h`, `app/settings/plays_store.c`, `app/include/plays_store.h`, `app/runtime/player.c`
  (`count_plays`, `begin_track`, `apply_next`), `app/ui/ui_nowplaying.c` (page 2), `docs/ui.md` (Now Playing, page 2),
  `docs/storage.md` (Playlists, play counts and On-The-Go).
- App: `docs/app/README.md` (The library; owner decisions 13 and 20), decisions 0005 and 0008.
- Platforms: Apple's `URLResourceValues.isExcludedFromBackup`; Android Auto Backup (`fullBackupContent`,
  `dataExtractionRules`, `noBackupFilesDir`). The behaviour is checked by the tests named above, on the CI simulator
  and emulator, not taken from the documentation alone.
