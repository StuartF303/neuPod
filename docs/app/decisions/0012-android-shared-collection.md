# 0012: Android's shared music collection

Status: Built (2026-11-25); the duplicate rule is a proposal awaiting the owner (Open questions) · Owner: Library engineer

## Context

- The spec (Getting music onto the phone): on Android the user picks folders, and the app "also offers the device's
  shared music collection". Owner decision 8: its tracks get embedded art only, because folder art would need a
  restricted image permission.
- Decision 0004 planned it: MediaStore audio, `READ_MEDIA_AUDIO` on Android 13 and later and `READ_EXTERNAL_STORAGE`
  (with `maxSdkVersion="32"`) before, asked for only when the user turns the collection on; items opened by their
  `content://media/...` URI on every version (Android 10 cannot read the `DATA` path without legacy storage).
- The app's minimum is Android 10 (API 29), so MediaStore's `RELATIVE_PATH` and `VOLUME_NAME` columns are always
  there.
- The firmware is the reference (constitution II): the device reads its tags from the file. MediaStore's own tag
  columns (`TITLE`, `ARTIST`, `ALBUM`, ...) come from Android's scanner and differ from the device's (other fallbacks,
  other text handling, a title made from the file name without its extension), so they are not used.
- Ratings and play counts are keyed by the device's path and size, `'/' + rel_path` (decision 0011).

## Decision

**The collection is one more root of the folder access plugin**, not a second scanner. `neupod_storage` gets one call,
`addSharedCollection()`, and a root kind, `sharedCollection`, with the fixed id `mediastore:audio`
(`sharedCollectionRootId`). Everything else is the folder path unchanged:

| | Picked folder (SAF) | Shared collection (MediaStore) |
| --- | --- | --- |
| Turned on | `pickFolder()`: the system picker, a persisted read-only tree grant | `addSharedCollection()`: the read permission if not held (the system's question), then the plugin's own "on" flag (`SharedPreferences`) |
| Listed by | `listRoots()`, in grant order | `listRoots()`, after the folders, while on; `available` is false when the permission was taken away in the phone's Settings |
| Walked by | `startWalk` / `nextWalkBatch`: one `DocumentsContract` query per directory | the same calls: one MediaStore query (`Audio.Media`, `VOLUME_EXTERNAL`, `IS_MUSIC != 0`), served a page at a time; always complete |
| `relPath` | path below the picked folder | `RELATIVE_PATH + DISPLAY_NAME`: the path from the top of the storage volume (`Music/Album/01 Song.flac`) |
| `uri` | the document URI in the tree | `content://media/<volume>/audio/media/<id>` |
| `size`, `modifiedMs` | the document's | `SIZE`, `DATE_MODIFIED` (seconds) x 1000 |
| Opened by | `openFd`: `ContentResolver.openFileDescriptor(uri, "r")`, read with `pread` | the same |
| Turned off | `removeRoot(id)`: the grant released | `removeRoot('mediastore:audio')`: the flag cleared (Android lets an app give up a runtime permission only from 13, and only at the next process end; the permission is left to the user) |
| `sources.kind` | `local_folder` | `media_store` (no schema change: `kind` is text, decision 0005) |

- **Tags are read from the file's bytes** by the app's firmware-ported readers, exactly as for a folder's files. The
  scanner never sees MediaStore's tag columns (the projection does not ask for them).
- **The device-style key works unchanged:** a collection track's key is `'/' + RELATIVE_PATH + DISPLAY_NAME` and its
  size, so ratings, play counts and the carry-over by audio fingerprint (decision 0011) apply as they do to folders.
- **Incremental rescan by `DATE_MODIFIED` and size.** The scanner's existing check (same size, mtime, URI and scan
  version: not reopened) needs nothing new. The whole collection is listed on every scan (one query; 20,000 rows is a
  few hundred milliseconds), which is also what tells the scanner a song has gone. `GENERATION_MODIFIED` (API 30+)
  would save no query, since deletions are only seen by listing.
- **Music only:** `IS_MUSIC != 0`, as Android's own music players do, so ringtones, notification and alarm sounds,
  recordings and podcasts are left out (Open question 3).
- **Hidden names** (a name or a folder starting with `.`) are skipped, as in a folder walk.
- **Two volumes, one path.** A track's path is unique in its source (`tracks(source_id, rel_path)`). If the phone's
  storage and an SD card both hold `Music/a.flac`, the phone's own is listed and the card's left out. Rare; noted.
- **Art:** embedded only (owner decision 8). The collection's walk lists only audio, so no cover file is ever found.
- **Turning it off** is the folder's Remove: the next scan deletes the source's tracks, and `track_stats` (another
  database) keeps their numbers, which come back when it is turned on again.
- **Background scan:** the scan is the same `LocalLibraryScanner` run, so the Android foreground service (decision
  0009) covers it with no change.
- **Read only:** the plugin adds no write call (the static guard, `test/read_only_guard_test.dart`, passes unchanged).

## The same file through a picked folder and the collection

A file in `Music/Library/A/01 x.flac` reaches the app as `A/01 x.flac` through a picked `Music/Library` and as
`Music/Library/A/01 x.flac` through the collection: two keys, two rows, two sets of numbers unless something chooses.

**Built (proposal): the picked folder lists it; the collection leaves it out.** The scanner knows where each picked
folder is from its tree URI (`content://com.android.externalstorage.documents/tree/primary%3AMusic%2FLibrary` is
volume `external_primary`, path `Music/Library`; an SD card's volume id is MediaStore's volume name in lower case) and
where each collection item is from its URI's volume and its relative path. An item inside a picked folder on the same
volume is skipped by the collection's walk (case ignored, as Android's shared storage ignores it). Folders from other
providers have no location and hide nothing. `lib/data/scan/shared_collection.dart`.

When the owner adds or removes a folder over songs already in the library, the keys change in one scan: one gone, one
arrived, the same audio fingerprint. That is decision 0011's move, so the numbers are carried to the new key
(tested: `test/data/scan/shared_collection_test.dart`, "picking a folder over collection songs carries their
numbers").

### Options for the owner

| | Rule | For | Against |
| --- | --- | --- | --- |
| **A** | No rule: both list it | Nothing to explain | The song is twice in Songs, its album, Shuffle; plays and ratings split over two keys |
| **B (built, recommended)** | The picked folder wins | Once in the library; it keeps the folder's cover art (decision 8 denies the collection that); the path is the one the user chose, which is likely nearer a card's layout (`Album/...`); the user's explicit pick beats the general switch | A rule to explain if anyone wonders why the collection's count is lower |
| **C** | The collection wins | Once in the library | Those songs lose folder art; their paths become storage paths (`Music/Library/...`); picking a folder inside the collection does nothing visible |
| **D** | One or the other: picking a folder turns the collection off, or the reverse | No overlap ever | Takes away a reasonable set-up (a folder on the SD card plus the phone's music) |

Changing B to A or C is a few lines in `LocalLibraryScanner` (the `insidePickedFolder` skip).

## Open questions (owner)

1. **Duplicates:** A, B (built), C or D above.
2. **The row's words** in Settings > Library: `Add Music on This Phone` while off; listed as `Music on This Phone` as a
   folder is while on, with `Turn Off` (the firmware's question, as Remove: `Turn off Music on This Phone?`,
   `n songs come off the list.`, `Select = Turn Off · Menu = Keep`) where a folder has Remove. No design draws it.
3. **Music only?** `IS_MUSIC` leaves out ringtones, notification and alarm sounds, recordings and podcasts that
   Android has flagged as such. The device lists every audio file on its card. Proposed: music only, as on Android's
   own players.
4. **R3 (later):** a collection track's key runs from the top of the phone's storage (`/Music/Album/...`); a card's
   paths run from the card's root. Matching the two for a ratings exchange is R3's to design; nothing is needed now.

## Tests

- `test/data/scan/shared_collection_test.dart`, over a fake MediaStore (a host folder served as the collection, with
  `content://media` URIs, MediaStore ids and `DATE_MODIFIED` in seconds, and SAF tree URIs for picked folders): tags
  from the bytes and the device's fallbacks, paths from the top of the storage, the `media_store` source, no folder
  art; a rescan that opens only new or changed items and drops what left; the duplicate rule; turning it off with the
  numbers kept and found again; the carry-over when a folder is picked over collection songs; the tree URI and volume
  parsing.
- `test/ui/screens/library_screen_test.dart` ("the shared music collection"), `test/data/platform/storage_folder_access_test.dart`.
- `integration_test/media_store_test.dart` on the CI emulator (API 29), through the real plugin: the instrumentation rule
  (`MainActivityTest.java`, only with `neupodSeedMediaStore=true`, which `tool/android_integration_tests.sh` passes for
  this file) grants the permission and inserts a WAV into MediaStore through `ContentResolver`; the test turns the
  collection on, finds it listed and the file in its walk, opens it, scans it with the app's scanner (duration read
  from the WAV header), turns it off and sees the song go. The permission dialog itself is not driven (it is the
  system's); with the permission held, turning on asks nothing. On iOS the same file checks that the call is refused as
  `unsupported`.

## Sources

- Android, media store (collections, `RELATIVE_PATH`, `VOLUME_NAME`, `IS_PENDING`, permissions by version):
  https://developer.android.com/training/data-storage/shared/media
- `MediaStore.Audio.AudioColumns.IS_MUSIC`, `MediaStore.VOLUME_EXTERNAL`:
  https://developer.android.com/reference/android/provider/MediaStore.Audio.AudioColumns
- Granular media permissions (Android 13): https://developer.android.com/about/versions/13/behavior-changes-13#granular-media-permissions
- `ExternalStorageProvider` document ids (`primary:Music`, `<uuid>:Music`): AOSP
  `packages/providers/MediaProvider` and `frameworks/base/packages/ExternalStorageProvider`.
