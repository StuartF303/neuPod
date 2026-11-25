// SPDX-License-Identifier: MPL-2.0

// The platform API of neupod_storage (decision 0004). After editing, run from
// mobile/packages/neupod_storage:
//
//   dart run pigeon --input pigeon/messages.dart
//
// and commit the generated Dart, Kotlin and Swift files.

import 'package:pigeon/pigeon.dart';

@ConfigurePigeon(
  PigeonOptions(
    dartPackageName: 'neupod_storage',
    dartOut: 'lib/src/messages.g.dart',
    copyrightHeader: 'pigeon/copyright.txt',
    kotlinOut:
        'android/src/main/kotlin/net/stuartfraser/neupod_storage/Messages.g.kt',
    kotlinOptions: KotlinOptions(package: 'net.stuartfraser.neupod_storage'),
    swiftOut: 'ios/neupod_storage/Sources/neupod_storage/Messages.g.swift',
  ),
)
/// Where a root comes from.
enum RootKind {
  /// iOS only: the app's own Documents folder, shown in Files under
  /// "On My iPhone" and in Finder. Always present; cannot be removed.
  appDocuments,

  /// A folder the user picked with the system picker (Android SAF tree,
  /// iOS security-scoped bookmark).
  pickedFolder,
}

/// Whether a file's bytes are on the device.
enum EntryState {
  /// On the device and readable.
  local,

  /// iOS only: an iCloud Drive file that is not downloaded. The app must not
  /// read it (reading would make iOS fetch it); owner decision 7.
  notDownloaded,
}

/// Where an iCloud file's download stands (owner decision 7). Read from the
/// file's metadata only: asking never reads its bytes.
enum DownloadState {
  /// Its bytes are on the device: the next scan reads it.
  downloaded,

  /// Not on the device, and iOS is not fetching it.
  notDownloaded,

  /// iOS is fetching it.
  downloading,

  /// iOS reported an error for its download (offline, no space, signed out
  /// of iCloud).
  failed,

  /// No file at that URI any more (deleted, moved, or its folder removed).
  missing,
}

/// A library folder the app holds read access to.
class StorageRoot {
  StorageRoot({
    required this.id,
    required this.displayName,
    required this.kind,
    required this.available,
  });

  /// Stable id: the tree URI on Android; `documents` or a generated id for a
  /// stored bookmark on iOS.
  final String id;

  /// The folder's name as the system shows it.
  final String displayName;

  final RootKind kind;

  /// False when access could not be restored (the grant was revoked, the
  /// bookmark no longer resolves, a drive is unplugged).
  final bool available;
}

/// A file under a root. Directories are walked, not returned.
class StorageEntry {
  StorageEntry({
    required this.relPath,
    required this.uri,
    required this.size,
    required this.modifiedMs,
    required this.state,
  });

  /// Path relative to the root, `/`-separated, with the real file name (for
  /// an iCloud placeholder, the name of the file it stands for).
  final String relPath;

  /// `content://` (Android) or `file://` (iOS); for an iCloud placeholder,
  /// the URL of the file it stands for, so the URI stays the same once the
  /// file is downloaded. Open it with
  /// `NeupodStorage.openReader`, or hand it to the audio engine.
  final String uri;

  /// Size in bytes; 0 when unknown.
  final int size;

  /// Last modified, milliseconds since the epoch; 0 when unknown.
  final int modifiedMs;

  final EntryState state;
}

/// One page of a walk.
class WalkBatch {
  WalkBatch({required this.entries, required this.done, required this.complete});

  final List<StorageEntry> entries;

  /// True on the last page; the walk is then released.
  final bool done;

  /// Meaningful on the last page: true only if every folder of the tree was
  /// listed. False if a subfolder could not be read, the tree was deeper than
  /// the depth cap, the provider or drive went away mid-walk, or the
  /// coordinated read failed. A caller must not treat files missing from an
  /// incomplete walk as deleted.
  final bool complete;
}

/// Read-only folder access. There is deliberately no call that writes,
/// moves or deletes a file.
@HostApi()
abstract class NeupodStorageApi {
  /// Shows the system folder picker. Returns the new (or already held) root,
  /// or null if the user cancelled. Access is read-only and persisted.
  @asyncCallback
  StorageRoot? pickFolder();

  /// The roots the app holds, in the order they were added. On iOS the
  /// Documents folder is first.
  @asyncCallback
  List<StorageRoot> listRoots();

  /// Releases a picked root (Android: the persisted grant; iOS: the bookmark
  /// and its security-scoped access) and cancels its walks. Files are not
  /// touched. Runs off the platform thread.
  @asyncCallback
  void removeRoot(String rootId);

  /// Starts a recursive walk of [rootId] and returns a walk id for
  /// [nextWalkBatch]. Throws `root-unavailable` if access is gone. Runs off
  /// the platform thread.
  @asyncCallback
  String startWalk(String rootId);

  /// Returns the next files of a walk, about [maxEntries] at a time (a page
  /// may run over by one directory's files). Runs off the platform thread.
  @asyncCallback
  WalkBatch nextWalkBatch(String walkId, int maxEntries);

  /// Stops a walk early and releases it.
  void cancelWalk(String walkId);

  /// Android only: opens [uri] read-only and returns a detached file
  /// descriptor, which the caller owns and must close (`FdStorageReader`
  /// does). iOS throws `unsupported`: `file://` URIs are read directly.
  /// Runs on its own background executor (a provider can block), never on
  /// the platform thread.
  @asyncCallback
  int openFd(String uri);

  /// iOS only: asks iOS to download the iCloud file at [uri] (a `file://`
  /// URI from a walk whose state was `notDownloaded`), with
  /// `FileManager.startDownloadingUbiquitousItem(at:)`. Returns at once;
  /// iOS fetches the file and the app reads nothing of it (owner decision
  /// 7). Throws `not-in-root` for a URI outside the held roots and
  /// `download-failed` when iOS refuses (not an iCloud file, signed out).
  /// Android throws `unsupported`.
  @asyncCallback
  void requestDownload(String uri);

  /// iOS only: the download state of each of [uris], in order, from file
  /// metadata alone (placeholder names, `ubiquitousItemDownloadingStatus`,
  /// the dataless flag): never a read of the file. Off the walk queue, so a
  /// slow walk does not hold it up. Android throws `unsupported`.
  @asyncCallback
  List<DownloadState> downloadStates(List<String> uris);

  /// A private directory for the app's own data (the library database),
  /// excluded from backups: Android `noBackupFilesDir`; iOS
  /// `Application Support/library`, created if needed and marked excluded
  /// from backup. Never the Documents folder, so not shown in Files or Finder.
  String appSupportPath();

  /// A private directory for the app's data that the phone's own backup
  /// keeps (owner decision 20: ratings and play counts survive a restore):
  /// Android `filesDir/stats`, which the app's backup rules include (and
  /// nothing else); iOS `Application Support/stats`, created if needed and
  /// marked as included in backups (`isExcludedFromBackup` false, read
  /// back). Never the Documents folder.
  String backedUpDataPath();

  /// iOS only: whether the file or folder at [path] is excluded from iCloud
  /// and device backups (`URLResourceValues.isExcludedFromBackup`), for
  /// tests. Throws `unsupported` on Android, where the backup rules decide.
  bool isExcludedFromBackup(String path);

  /// iOS debug builds only: stores a bookmark for the folder at [path] as if
  /// it had been picked, so integration tests can exercise bookmark storage,
  /// resolution and walking without driving the system picker UI. Throws
  /// `unsupported` in release builds and on Android.
  StorageRoot debugAddRoot(String path);
}
