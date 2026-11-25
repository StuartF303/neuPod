# 0011: Ratings, play counts and last played

Status: Accepted in part (2026-11-25) · Owner: Library engineer · **Three owner questions open** (below)

## Context

- The spec (`README.md`, The library): "Ratings (0 to 5), play counts, last played and skip counts are kept per track in
  the app's own database, keyed so that they survive a rescan and a file move within the library." Decision 0005
  planned a `track_stats` table keyed by a content fingerprint (`track_key`) for exactly that.
- The firmware is the reference (constitution II). It keeps ratings in `app/settings/ratings_store.c` and plays in
  `app/settings/plays_store.c`, both **keyed by the file's path and size**: `ratings_key(path, size)`, an FNV-1a hash of
  the path mixed with the size (`ratings_store.c:26`; `plays_store.h`, "An entry is keyed like a rating"). A rebuilt
  library keeps them; a moved file, or a retag that changes the size, starts again at nothing.
- **The two disagree on a move.** The device's key survives a rescan but not a move; the spec asks for both. This note
  builds what does not depend on the answer and leaves the key to the owner (question 1).
- The firmware has **no skip count** anywhere (`grep -ri skip app/` finds the player's unplayable-track skips and button
  skips, nothing counted). Where the firmware is silent the owner decides (question 2).
- Decision 0005 excluded the database from phone backups "for now", to be revisited when ratings arrive (question 3).

## The firmware rules, ported

All in `mobile/lib/domain/library/play_stats.dart`, checked against the compiled C by
`test/domain/library/play_stats_vectors_test.dart` with `test/fixtures/plays/firmware_vectors.json`, which
`scripts/app/gen-plays-vectors.sh` prints from the firmware's own `play_tracker.c`, `ratings_store.c` and
`plays_store.c` (`scripts/app/plays_vectors.c`). CI regenerates the file and fails on a difference.

| Rule | Firmware | App |
| --- | --- | --- |
| A track start counts as a play once, when half its length has been heard, or 30 s when the length is unknown, or when it ended by itself after 3 s heard | `ptrk_update`, `app/runtime/play_tracker.c:12`; constants `app/include/play_tracker.h:23-25` | `PlayTracker.update` |
| Only time spent playing counts (a seek or scan does not move it); a held-up pass adds at most 1 s; a new track start begins again; times are `uint32_t` and wrap | same | same (masked to 32 bits) |
| A pass every 100 ms on the monitor task, whatever screen is up; playing = `PLAYER_PLAYING` and not held for a Bluetooth headset; ended = `PLAYER_ENDED`; a new `track_id` on every start, a skip and a gapless handoff, not on a seek | `count_plays`, `app/runtime/player.c:441`; `app/runtime/runtime.c:328`; `player.h:59` | `PlayCounter` (`domain/library/play_counter.dart`): a 100 ms timer while a track is current; playing = `PlayerState.isPlaying` (ready, asked to play, not held back); ended = `PlaybackStatus.ended`; a start = a new queue or another index |
| A rating is 0 to 5; 0 clears it; anything above 5 is ignored | `ratings_set`, `ratings_store.c:45` | `PlayStats.rated` |
| A counted play adds one (saturating at 65535, `uint16_t`) and stamps "last played" with the serial; any touch (a play, a note) counts the serial up and stamps the entry | `apply`, `plays_store.c:50-91` (saturation `:84`) | `PlayStats.touched`; the serial is the largest stamp, so it lives in the table |
| "Last played" is a serial, not a time: "the player has no real clock on every board" | `plays_store.h` | kept as a serial, so the order matches the device's |
| The wheel sets the stars one per turn whatever the turn's size, clockwise more, 0 to 5; a haptic step only when they change; saved at once | `p2_scroll`, `app/ui/ui_nowplaying.c:788-802` | `starsAfterTurn`; `NowPlayingScreen` page 2 |
| A rating of 4 stars or more notes the track (zero plays) so Top rated can open it | `ui_nowplaying.c:795` | `TrackStatsStore.rate` |

## Storage (decided here, routine)

- **Schema version 5** adds one table, `track_stats(path, size, rating, plays, last_played, stamp)`, primary key
  `(path, size)`, CHECK `rating` 0 to 5 and `plays` 0 to 65535. Step `from4To5` creates it, inside the one upgrade
  transaction of decision 0008; snapshot `drift_schemas/library/drift_schema_v5.json`; migration test "4 to 5" and the
  generic every-row-kept tests cover it. The guard test now allows `sources`, `tracks` and `track_stats`, and checks
  `track_stats` has no foreign key.
- **Key: the device's identity, as an interim until question 1 is answered.** `path` is the device's form of the
  track's path, `'/' + tracks.rel_path`, as a card's root gives it; `size` is the file's size. The app stores the two
  themselves, not the firmware's 32-bit hash, so two tracks never share numbers through a hash collision (the device's
  128-entry table makes that rare; at 20,000 tracks it would not be). The hash is a function of the two and can be
  computed when R3 exchanges ratings with a device.
- **Nothing ties the table to `tracks`.** No foreign key, no cascade: a rescan, a re-read file, a new scan version, a
  removed folder or a deleted file never touches a row. A folder added back finds its numbers again, as a card does on
  the device. Tested in `test/data/scan/local_library_scanner_test.dart` ("ratings and play counts" group).
- **One table for the firmware's two stores.** A row with `stamp` 0 is a rating the plays store would not know; a row
  with no rating and `stamp` 0 is deleted.
- **The app keeps every track; the device keeps 128 (24 ratings on the Pico 2 W) and evicts.** Memory lets a device do
  less, never differently (constitution IV), so the app does not port the eviction, the 96-byte path limit
  (`plays_store.c:101`) or the copy of title and artist the device keeps for lists it draws without a library (the app
  has the library).
- The store writes only this table, in one transaction per change. It never writes a music file, a music folder or a
  card's `/.neupod` (read-only guard allow-list entry: the drift `delete` of an emptied row).

## Known gaps (logged in PROGRESS)

- **A restart of the same queue entry is not a new start yet.** The engine does not report one: Previous after 3 s and
  (phase 4) repeat one restart the same index, so a second play in a row of the same entry is not counted. On the
  device they are new starts (`begin_track`, `player.c`). Needs a start counter from `neupod_audio` (audio engineer).
- Plays are counted by Dart. Where Android keeps playing after the Flutter engine has gone (the media service alone),
  nothing counts them until the app is back.
- The device's Now Playing page 2 puts the stars above the rows (wheel up from the first row focuses them, a soft
  accent box behind, lit stars in the accent colour and the rest in the line grey). The app keeps owner decision 13's
  Rating row and its existing star skin (filled and outlined stars in the secondary and tertiary inks). Whether the
  colours should follow the firmware is a UI styles question for the owner, not changed here.
- Most played, Top rated and Unplayed (Music > Playlists on the device) and Settings > Reset ratings and play counts
  belong to phase 6; the numbers they need are stored now.

## Owner questions

### 1. What keys a track's numbers: the device's path and size, or something that survives a move?

The device loses a rating when a file is moved, or retagged so its size changes; the spec says the app keeps it.

- **A. The device's key only (built now).** Exactly the firmware: survives a rescan and a folder removed and added
  back; a move or a size-changing retag starts again. The spec's "and a file move" is struck.
- **B. A content fingerprint** (decision 0005's `track_key`: a hash of the audio payload, or FLAC's STREAMINFO MD5).
  Survives a move and a retag. Differs from the device, so the device would have to adopt it for one player, and R3's
  exchange would match on it. Costs about 128 KiB read per new file in a scan.
- **C. The device's key, carried over on a move.** The key stays `(path, size)`; when a rescan finds a track gone and
  a new one with the same audio fingerprint, it re-keys that row to the new path and size. The device's identity is
  kept (and R3's exchange stays simple); the app also follows moves and retags. The firmware could do the same when it
  rebuilds its library, so both behave alike.

**Recommendation: C.** It keeps the firmware's key, meets the spec, and is additive: the table and everything built
here stay as they are; only the scanner gains the carry-over (with the fingerprint of decision 0005).

### 2. Skip counts: the firmware has none

- **A. Drop them from R1's spec**, as the device does not count skips (recommended, constitution I: one player).
- **B. Define the rule in the firmware first** (for example: Next pressed before the play counted), then port it.
- **C. An app-only rule.** Not recommended: the app would count something the device does not.

### 3. Should ratings and play counts survive a phone restore?

The library database is excluded from backups (decision 0005: Android `noBackupFilesDir`, iOS excluded from backup).
Ratings and play counts are now in it and cannot be rebuilt.

- **A. Keep it excluded.** A restored or new phone starts with no ratings. Nothing about the owner's library goes to
  Google or Apple backups.
- **B. Back up the library database.** Ratings survive, and so do the paths and tags of every track, in the phone's
  cloud backup.
- **C. Move `track_stats` to its own small database in the backed-up area.** Ratings survive with only paths, sizes
  and numbers; the rebuildable library stays out of backups. Costs a second database and its own migrations.

**Recommendation: C** if the owner wants ratings to survive a restore, otherwise A (the current state). This changes
what leaves the phone, so it is the owner's call.

## Sources

- Firmware: `app/runtime/play_tracker.c`, `app/include/play_tracker.h`, `app/settings/ratings_store.c`,
  `app/include/ratings_store.h`, `app/settings/plays_store.c`, `app/include/plays_store.h`, `app/runtime/player.c`
  (`count_plays`, `begin_track`, `apply_next`), `app/ui/ui_nowplaying.c` (page 2), `docs/ui.md` (Now Playing, page 2),
  `docs/storage.md` (Playlists, play counts and On-The-Go).
- App: `docs/app/README.md` (The library; owner decision 13), decisions 0005 and 0008.
