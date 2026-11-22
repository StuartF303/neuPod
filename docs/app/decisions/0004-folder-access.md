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
   seekable); confirm we detect and grey it.
5. `neupod_audio` plays a `content://` tree URI and a MediaStore URI gaplessly on Android, and a
   bookmarked iCloud Drive file URL on iOS after relaunch.
6. iPhone: drag a folder in Finder into Documents; pick an iCloud Drive folder with some files evicted; confirm the
   placeholder names and status keys on the current iOS; relaunch and resolve the bookmark; a USB drive unplugged.
7. Android 10 device or emulator: MediaStore query with `READ_EXTERNAL_STORAGE`, URI reads work without legacy storage.

## Implementation (phase 1, 2026-11-22)

Built as decided, in `mobile/packages/neupod_storage` (Kotlin + Swift, Pigeon 30.0.4 as a dev dependency only), used
by `mobile/lib/data/platform/storage_folder_access.dart`. Where it differs from the decision above, and why:

- **Calls.** `pickFolder`, `listRoots`, `removeRoot`, `startWalk` / `nextWalkBatch` / `cancelWalk`, `openFd`
  (Android) and `debugAddRoot` (iOS debug builds only). `mediaStoreAudio` is left to its own phase 3 item ("Android:
  the device's shared music collection"), so phase 1 requests **no permission at all** on either platform.
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
