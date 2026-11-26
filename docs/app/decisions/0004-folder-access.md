# 0004: Folder access

Status: Accepted (phase 1, 2026-11-22; see Implementation) · Owner: Library engineer

## Context

- Android: pick one or more folders with the system picker (SAF `ACTION_OPEN_DOCUMENT_TREE`); keep **read-only**
  access across restarts (`takePersistableUriPermission`).
- Android: also offer the shared music collection (MediaStore audio); permission `READ_MEDIA_AUDIO` on API 33+,
  `READ_EXTERNAL_STORAGE` (with `maxSdkVersion="32"`) below.
- iOS: the app's Documents folder visible in Files and Finder (`UIFileSharingEnabled`,
  `LSSupportsOpeningDocumentsInPlace`); plus folders picked elsewhere in Files (iCloud Drive, external drive), kept
  with a bookmark of the security-scoped URL.
- iOS: iCloud files that are not downloaded are detected and handled, not mistaken for missing or broken files.
- Recursive listing with size and last-modified for 20,000 tracks: a no-change rescan under 10 s.
- Random-access reads of file headers for the tag reader ([0003](0003-tag-reader.md)) from a background isolate,
  without a platform-channel hop per read.
- Hand the audio engine something it can open natively and gaplessly.
- Never write, move or delete; request nothing beyond what reading needs. No network.
- Minimum OS (owner, 2026-11-21): iOS 18+, Android 10 (API 29)+. Flutter 3.50 itself supports Android 24+ and iOS 15+.

## Options considered

Checked on pub.dev, GitHub and the platform docs on 2026-11-21.

| Option | Version | Last release | Licence | Platforms | Tree pick + persist | Fast listing (projection) | Byte access | iOS bookmark | Maintained |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Own small plugin in `mobile/`** (Kotlin + Swift, Pigeon 30.0.4 for the API) | n/a | n/a | MPL-2.0 (ours); Pigeon BSD-3 | both | yes, read-only flags | yes, recursive in Kotlin, one call | detached fd (Android), path (iOS) | yes | us |
| `saf_util` + `saf_stream` | 3.1.1 / 4.0.3 | 2026-09-30 / 2026-10-21 | BSD-3-Clause | Android only | yes (`persistablePermission`) | yes, `DocumentsContract` query with projection, one level per call | `getFileDescriptor` (fd int), streams | - | yes |
| `file_picker` (`getDirectoryPath`) | 13.4.1 | 2026-11-21 | MIT | both | pick only; no persisted iOS bookmark API | no listing | - | no | yes |
| `shared_storage` | 0.8.1 | 2024-02-16 | MIT | Android | - | - | - | - | discontinued |
| `docman`, `saf`, `flutter_saf`, `document_tree_android` | 1.2.0 / 2.1.2 / 0.1.0 / 1.0.0 | 2025-01 to 2026-11 | MIT/BSD-3 | Android | yes | varies | varies | - | small or new |
| `file_anchor` (+ `_ios`, `_android`) | 0.1.0 | 2026-09-13 | MIT | both | yes | unknown | unknown | yes | one release, no users |
| `directory_bookmarks` | 0.1.2 | 2024-11-26 | MIT | Android, macOS (not iOS) | - | - | - | macOS only | stale |
| `on_audio_query` (MediaStore) | 2.9.0 | 2023-05-15 | BSD-3-Clause | Android, iOS | n/a | MediaStore query | - | n/a | stale |
| `media_store_plus` | 0.1.3 | 2024-09-22 | MIT | Android | n/a | write-oriented | - | n/a | stale |

**Android listing, the fast path.** `DocumentFile.listFiles()` then `length()`/`lastModified()` costs one binder
query per file per attribute; the Android guide itself warns that iterating a large tree "might reduce performance",
and field reports put a direct query hundreds of times faster. The fast path is one
`ContentResolver.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId), [DOCUMENT_ID, DISPLAY_NAME,
MIME_TYPE, SIZE, LAST_MODIFIED], ...)` per directory, walked breadth-first in Kotlin on a background thread, results
streamed to Dart in batches. 300 CDs is roughly 600 directories, so a rescan is about 600 queries plus a database diff.
`saf_util.list` already does this query, one level per channel call; our plugin does the whole walk in one call.

**Android byte access.** `ContentResolver.openFileDescriptor(uri, "r")` then `ParcelFileDescriptor.detachFd()` gives
an int fd owned by Dart; the tag reader does `pread` and `close` through `dart:ffi` (package `ffi`, BSD-3) in its
isolate, with no channel traffic per read. (Alternative to test: open `/proc/self/fd/N` with `dart:io`.) MediaStore
items use the same route with their `content://media/...` URI; on API 30+ the `DATA` path is also readable with
`READ_MEDIA_AUDIO`, but not on Android 10 without `requestLegacyExternalStorage`, so we use the URI on every version.

**iOS.** Documents needs no permission: the two Info.plist keys make it appear under On My iPhone and in Finder.
Other folders: `UIDocumentPickerViewController(forOpeningContentTypes: [.folder])` (iOS 13+) returns a security-scoped
URL that covers the whole tree "including any new items"; we store `bookmarkData()` (`.withSecurityScope` is
macOS-only; iOS bookmarks of picker URLs are implicitly scoped), resolve it at launch, re-save if stale, and keep
`startAccessingSecurityScopedResource()` active for each library root while the app runs. Children of the root are
then ordinary paths that `dart:io` and AVFoundation can open from any isolate. Listing is `FileManager` enumeration
with `fileSizeKey`, `contentModificationDateKey`, `ubiquitousItemDownloadingStatusKey`, inside `NSFileCoordinator`
as Apple asks. iCloud items not on the device appear as hidden `.Name.ext.icloud` placeholders (or with status
`notDownloaded`); we list them as tracks greyed `Not downloaded` with folder-convention names, and never download
on our own (see risks).

## Decision

Build one small in-repo plugin (`mobile/packages/neupod_storage`, Kotlin + Swift, API defined with Pigeon) with five
calls: `pickFolder()`, `listRoots()` / `removeRoot()`, `walk(root) -> stream of {relPath, size, mtime, uri, state}`,
`openRead(uri) -> fd` (Android) / path (iOS), and `mediaStoreAudio(sinceGeneration)` on Android. No maintained
package covers iOS folder bookmarks, and the Android packages either list one level per call or are stale; the native
code needed is a few hundred lines on each side. `saf_util` (BSD-3, active) is the fallback for the Android half if
the plugin slips.

The audio engine gets a **URI string**: `content://...` on Android (Media3's `DefaultDataSource` opens `content`
URIs itself, and the persisted grant belongs to the whole app process), `file://...` under an actively accessed root
on iOS. The URI goes to `neupod_audio` ([0001](0001-audio-engine.md)): Media3 opens `content://` itself, and on iOS
AVAudioFile opens the `file://` URL. **Only `neupod_storage` resolves bookmarks and starts or stops security-scoped
access**; the audio plugin never does, so neither can end access the other still needs. Not an fd and not a Dart byte
stream (a Dart stream source would need a localhost HTTP proxy, which fits badly with "no network connection").

## Consequences and risks

- **Android 11+ cannot pick the root of internal storage, the root of a reliable SD card, or `Download`** as a tree.
  Music in `Music/` or any subfolder is fine. The UI must say this when the picker refuses.
- **MediaStore source and folder art:** `READ_MEDIA_AUDIO` covers audio only. `Folder.jpg`/`cover.jpg` next to the
  tracks are images, and `READ_MEDIA_IMAGES` is restricted by Play policy. Tracks from the shared collection get
  embedded art only; folder-art fallback works for SAF-picked folders. **Resolved: owner decision 8.** Spike:
  `ContentResolver.loadThumbnail` on the album URI may recover some folder art under `READ_MEDIA_AUDIO`.
- **iCloud not downloaded:** reading or playing an evicted file makes iOS fetch it from iCloud. R1 promises no network
  connection. **Resolved: owner decision 7**, a Download action that asks iOS to fetch; the app never reads an
  evicted file without that tap. iOS 18.4+ reportedly changed when evicted files are refreshed (forum report, unanswered); test.
- **Scan surviving the app being closed:** Android can keep a scan going in a foreground service or WorkManager.
  iOS cannot run an arbitrary long task after the user closes the app; the scan checkpoints and resumes at next
  launch, and keeps going while music plays in the background (the app is then not suspended). **Resolved: owner
  decision 8.**
- The iOS picker grants read-write to the tree; read-only is enforced by our code only (no write API in the plugin).
  On Android we request `FLAG_GRANT_READ_URI_PERMISSION` only.
- Removable drives on iOS: a bookmark to an unplugged drive fails to resolve; the root shows as unavailable, its tracks
  stay in the library greyed until the user removes the folder.
- Persisted SAF grants are capped (128, raised to 512 in Android 11, per AOSP and CommonsWare); we hold one per root.
- Permissions requested: Android `READ_MEDIA_AUDIO` (33+) and `READ_EXTERNAL_STORAGE` with `maxSdkVersion="32"`, only
  when the user turns on the shared collection; none for SAF. iOS: none.

## To verify in the phase 1 spike

1. Android emulator (API 29 and 36): pick `Music/Library`, reboot, confirm the grant survives
   (`persistedUriPermissions`).
2. Walk a generated 20,000-file, 2,000-directory tree: time the Kotlin walk and the Dart diff; target under 5 s total
   no-change rescan on the emulator; compare with `DocumentFile` to record the gap.
3. `detachFd` + FFI `pread` from a background isolate: 5,000 header reads; check fds are always closed (count
   `/proc/self/fd`). Try `/proc/self/fd/N` with `RandomAccessFile` as the simpler alternative.
4. Same fd path on a non-local provider (Google Drive if installed): `openFileDescriptor` may return a pipe (not
   seekable); confirm we detect and grey it. (Moot for R1: the picker is local-only, see Implementation.)
5. `neupod_audio` plays a `content://` tree URI and a MediaStore URI gaplessly on Android, and a
   bookmarked iCloud Drive file URL on iOS after relaunch.
6. iPhone: drag a folder in Finder into Documents; pick an iCloud Drive folder with some files evicted; confirm the
   placeholder names and status keys on the current iOS; relaunch and resolve the bookmark; a USB drive unplugged.
7. Android 10 device or emulator: MediaStore query with `READ_EXTERNAL_STORAGE`, URI reads work without legacy storage.

## Implementation (phase 1, 2026-11-22)

Built as decided, in `mobile/packages/neupod_storage` (Kotlin + Swift, Pigeon 30.0.4 as a dev dependency only), used
by `mobile/lib/data/platform/storage_folder_access.dart`. Where it differs from the decision above, and why:

- **Calls.** `pickFolder`, `listRoots`, `removeRoot`, `startWalk` / `nextWalkBatch` / `cancelWalk`, `openFd`
  (Android), `appSupportPath` (the private directory for the library database, so `path_provider` is not needed; see
  [0005](0005-database.md)) and `debugAddRoot` (iOS debug builds only). `mediaStoreAudio` is left to its own phase 3 item ("Android:
  the device's shared music collection"), so phase 1 requests **no permission at all** on either platform. (Phase 3:
  built as `addSharedCollection` plus a root of kind `sharedCollection` that the same walk calls list, see
  [0012](0012-android-shared-collection.md).)
- **Walk as pages, not a stream.** The walk is pull-based: each `nextWalkBatch` continues the breadth-first
  DocumentsContract walk (Android) or the `FileManager` enumerator (iOS) on a background thread until it has about N
  files. Dart wraps it as a `Stream<List<StorageEntry>>`; cancelling the subscription calls `cancelWalk`. This gives
  back-pressure and cancel with plain host-API calls, where a Pigeon event channel cannot take a root argument.
- **Callback-style async (`@asyncCallback`).** Pigeon 28+ generates Kotlin `suspend` functions for `@async`, which
  would add `kotlinx-coroutines-android` to the app. The callback form needs no new shipped library.
- **No `ffi` package.** `FdStorageReader` calls `pread`/`lseek`/`close` (the `64` variants where libc has them) through
  `dart:ffi` and `DynamicLibrary.process()`, with `malloc`/`free` from libc too, so nothing new ships. `lseek` to the
  end gives the length and fails on a pipe, which is how a non-seekable provider descriptor is detected and refused.
- **iOS bookmarks** are kept in the plugin's own `UserDefaults` key, resolved and held open (one
  `startAccessingSecurityScopedResource` per root) when the plugin registers at launch, and re-saved when stale.
  Listing runs inside `NSFileCoordinator` with `.immediatelyAvailableMetadataOnly`, one coordinated read per page.
  iCloud placeholders (`.Name.ext.icloud`) and items whose downloading status is `notDownloaded` come back with
  `state = notDownloaded` and their real name; other hidden files are skipped. The privacy manifest declares the file
  timestamp (3B52.1, C617.1) and `UserDefaults` (CA92.1) reasons.
- **Test hook.** `debugAddRoot(path)` stores a bookmark exactly as a pick does, so the iOS integration test covers
  bookmark storage, resolution after relaunch, walking and reading. It throws in release builds. The picker UI itself
  is driven on Android (adb, `scripts/app/drive_saf_picker.py`) but not on the iOS simulator.
- **Hidden entries.** Names starting with `.` are skipped on both platforms (except iCloud placeholders on iOS).
- **Complete or not.** The last page of a walk says whether every folder was listed (`complete`). It is false when a
  subfolder could not be read (Android: the child query failed; iOS: the enumerator's error handler fired), when the
  tree is deeper than 64 levels or a document id repeats (a provider loop), when the coordinated read fails, or when the
  root is no longer reachable at the end (an unplugged drive makes the enumerator simply run dry). The scanner deletes
  tracks for files missing from a walk only after a complete one.
- **Threading.** Every ContentResolver call (roots, grants, walks) runs on one background executor and `openFd` on a
  second one, so a provider that blocks an open cannot stall a walk or the platform thread (`removeRoot`, `startWalk`
  and `openFd` are asynchronous in the API). On iOS all bookmark, access and walk state lives on one serial queue;
  bookmarks are resolved there at launch, not on the main thread, and a bookmark that did not resolve is tried again on
  every `listRoots`. `removeRoot` cancels the root's walks before giving up access.
- **On-device storage only (lead decision, 2026-11-22, under R1's no-network rule).** The Android picker is opened with
  `EXTRA_LOCAL_ONLY`, so cloud providers (Drive and the like), whose reads are network fetches, are not offered, and
  entries flagged `FLAG_VIRTUAL_DOCUMENT` (no bytes of their own) are skipped. Spike item 4 (a non-local provider) is
  therefore moot for R1; the pipe check in `FdStorageReader` stays as a guard. On iOS the picker cannot be limited this
  way; iCloud Drive is covered by owner decision 7 (not-downloaded files are never read).
- **Handles.** `openHandle` takes the `StorageEntry` and refuses a not-downloaded one by construction. A `ReaderHandle`
  is used exactly once (`open` or `discard`); `pread` is retried on `EINTR`, and failures carry `errno`.

## iCloud files not downloaded (phase 3, 2026-11-25)

Owner decision 7: such files are listed with a Download action; the app never fetches them itself, and a tap asks iOS to
download the file. Built as follows.

- **Detection, metadata only** (`ICloudItem` in `NeupodStoragePlugin.swift`). A file is not downloaded when any of these
  holds: it is a hidden placeholder `.Name.ext.icloud`; its `ubiquitousItemDownloadingStatus` is `notDownloaded`; or
  `lstat` shows the APFS dataless flag (`SF_DATALESS`), which File Provider sets on any file whose content is not local,
  so a third-party provider that reports no downloading status is caught too (the risk noted below). `lstat` and
  resource values read the inode and the provider's metadata; neither materialises the file. A placeholder is listed
  under the real file's name **and URL**, so the track's URI does not change once the file arrives. Size is 0.
- **Security-scoped bookmarks.** The bookmark of a picked iCloud Drive folder covers the files in it whether or not they
  are downloaded; access is held for the root as before, so metadata reads, the download request and the later read all
  work under it. Nothing new is bookmarked per file.
- **Never blocking.** The walk's coordinated read keeps `.immediatelyAvailableMetadataOnly` and now has a watchdog: if
  the coordination has not started within 15 s (another process holding a write, a provider not answering), the
  coordinator is cancelled and the walk ends incomplete, so the scan keeps what it had. Each walk lists its pages on its
  own serial queue, not the plugin's: a stuck enumerator holds up only its own walk, never `listRoots`, `removeRoot`,
  `requestDownload` or another root's walk. The Dart side gives up on `startWalk` and on each walk page after 2 minutes
  and on `listRoots` after 30 s (`StorageException('timeout')`; the scan then keeps what it had). Those are the limits:
  a `listRoots` stuck in the system (a bookmark resolving against a gone drive) still holds the plugin queue until the
  system answers, but no caller waits on it past 30 s. The scanner never opens a not-downloaded entry (`openHandle`
  refuses it by construction), nor a cover file that is not downloaded.
- **Listing.** A new not-downloaded file is listed by its file name (the device's untagged fallback), greyed
  `Not downloaded`. A file read before and then evicted keeps its tags, size and audio fingerprint, so it is still
  listed by its title and its ratings and play counts are still found; eviction is not a move. While it stays away a
  rescan does not touch it; once it is local the next scan reads it, whatever its size and time say.
- **Art.** A not-downloaded track offers no embedded-art source on its own file (the row keeps where its picture is,
  so the source and the cache key come back with the file); its folder's cover file is still offered. A walk that sees
  the cover file itself not downloaded clears it from the folder's tracks, even when the walk is incomplete. And on iOS
  the art cache's opener reads the source's download state first and refuses anything not on the phone, so a picture
  is never the reason iOS fetches a file, even before a scan has seen an eviction.
- **The Download action** (`requestDownload` calls `FileManager.startDownloadingUbiquitousItem(at:)` for a file under a
  held root, and nothing else). Then `downloadStates` is polled every 2 s for the files asked for only, from metadata
  (`downloaded`, `notDownloaded`, `downloading`, `failed` from `ubiquitousItemDownloadingError`, `missing`), for at most
  10 minutes, while the app is in front. When a file is `downloaded` the ordinary incremental scan runs, 2.5 s after
  the last file arrived (songs tapped one after another share one walk), and picks it up with its tags and art. If
  that scan still lists a file as not downloaded (its metadata lagging the state read), the row stays `Downloading` and
  it is scanned once more; then the library's word stands. A downloading error the file already had before the
  request is not reported as `failed` (iOS keeps the last error on an item until a new attempt replaces it): the plugin
  notes it at the request and reports only a different one.
- **Why a poll, not `NSMetadataQuery`.** A metadata query sees only the app's own ubiquity container, which needs the
  iCloud entitlement and a container the app does not have; it cannot watch a folder the user picked in iCloud Drive. An
  `NSFilePresenter` per file would work but adds native state and lifecycle for the same result. Rescanning only on the
  next return to the front would leave the row stale while the user waits. The poll is a handful of metadata reads.
- **Data safety.** `startDownloadingUbiquitousItem` is an OS action on the user's own file: iOS fetches its content
  unchanged. The read-only guard now forbids the iCloud calls that change a file's place or copy (`setUbiquitous`,
  `evictUbiquitousItem`) and allows the one download call, with its reason.
- **A queued song evicted later** (lead call, 2026-11-26, under decisions 7 and 19). A song can be evicted after it was
  queued. neupod_audio's engine checks the file's download state from metadata (`LocalFile.swift`, the same three
  signs as the walk) before it opens the current item or pre-schedules the next, and fails one that is not on the
  phone as `Not downloaded`; the player skips it as it skips an unplayable song, so AVFoundation never fetches it. The
  lock screen reads embedded art only from a file on the phone. neupod_audio's privacy manifest declares the file
  timestamp API for its `lstat`. The simulator test queues a placeholder-only song before a real one: the real one
  plays.
- **Search** rebuilds its index after every finished scan (it watches the library view), and its rows read
  `Downloading` once asked for.
- **Limits.** `requestDownload` gives up after 30 s on the Dart side. The notes of a file's earlier downloading error
  are dropped once its request settles (downloaded, failed or gone) and capped at 256 files.
- **Tests.** Unit: the scanner with walks that report files away and back; the state-to-step mapping and the controller
  with a fake platform; the row and the action in the shell. iOS simulator (`integration_test/folder_access_test.dart`):
  a real placeholder file in a bookmarked folder is walked, stated and scanned without being read, and a download
  request for it is refused or ignored without touching it. Also: a not-downloaded song's art has no source on its own
  file; a cover file evicted later is cleared after an incomplete walk; the iOS art opener refuses every state but
  `downloaded`; songs arriving together share a scan; the retry; a reload keeps the bar on the same song. A real
  evicted iCloud file, the status key and the dataless
  flag cannot be made on the simulator (no iCloud account; the flag is set only by the system): that is the owner's
  device check (PROGRESS, phase 3).

For the device check (phase 1) and phase 3:

- Third-party File Provider extensions may present dataless files without `ubiquitousItemDownloadingStatus`; the walk
  then reports them as local, and reading them may make the provider fetch them. Check on the owner's iPhone with any
  provider apps installed.
- Nested or duplicate roots (a folder picked inside another, or Documents picked as a folder) are not detected yet:
  their files would be scanned twice. Phase 3 (incremental rescan, removing a folder) handles it.


## Sources

All checked 2026-11-21.

- Android, documents and other files (tree picker, Android 11 restrictions, persist permissions, performance caution):
  https://developer.android.com/training/data-storage/shared/documents-files
- Android, media store (permissions by API level, direct paths on 11+, Android 10 legacy flag, `DATA` column):
  https://developer.android.com/training/data-storage/shared/media
- DocumentsContract: https://developer.android.com/reference/android/provider/DocumentsContract
- CommonsWare, Scoped Storage Stories: DocumentsContract and listFiles() Woe:
  https://commonsware.com/blog/2019/11/23/scoped-storage-stories-documentscontract.html ;
  https://commonsware.com/blog/2019/12/14/scoped-storage-stories-listfiles-woe.html
- Persisted grant limit: https://commonsware.com/blog/2020/06/13/count-your-saf-uri-permission-grants.html ;
  https://issuetracker.google.com/149315521
- Apple, Providing access to directories: https://developer.apple.com/documentation/uikit/providing-access-to-directories
- Apple, `bookmarkData(options:...)`, `withSecurityScope` (macOS only), `startDownloadingUbiquitousItem(at:)`,
  `UIFileSharingEnabled`, `LSSupportsOpeningDocumentsInPlace`: https://developer.apple.com/documentation/foundation/url/bookmarkdata(options:includingresourcevaluesforkeys:relativeto:) ;
  https://developer.apple.com/documentation/foundation/nsurl/bookmarkcreationoptions/withsecurityscope ;
  https://developer.apple.com/documentation/foundation/filemanager/startdownloadingubiquitousitem(at:) ;
  https://developer.apple.com/documentation/bundleresources/information-property-list/uifilesharingenabled ;
  https://developer.apple.com/documentation/bundleresources/information-property-list/lssupportsopeningdocumentsinplace
- iCloud placeholders: https://fatbobman.com/en/posts/advanced-icloud-documents/ ; iOS 18.4 report:
  https://developer.apple.com/forums/thread/785030
- saf_util: https://pub.dev/packages/saf_util , https://github.com/flutter-cavalry/saf_util (`SafUtilPlugin.kt`);
  saf_stream: https://pub.dev/packages/saf_stream
- file_picker: https://pub.dev/packages/file_picker ; shared_storage: https://pub.dev/packages/shared_storage ;
  file_anchor: https://pub.dev/packages/file_anchor ; directory_bookmarks: https://pub.dev/packages/directory_bookmarks ;
  on_audio_query: https://pub.dev/packages/on_audio_query ; media_store_plus: https://pub.dev/packages/media_store_plus
- just_audio (Media3 `DefaultDataSource`, `StreamAudioSource` proxy): https://pub.dev/packages/just_audio ;
  https://github.com/ryanheise/just_audio
- Pigeon: https://pub.dev/packages/pigeon ; ffi: https://pub.dev/packages/ffi
- Flutter supported platforms: https://docs.flutter.dev/reference/supported-platforms
