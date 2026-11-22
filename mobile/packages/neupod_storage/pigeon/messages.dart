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

  /// `content://` (Android) or `file://` (iOS). Open it with
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
  WalkBatch({required this.entries, required this.done});

  final List<StorageEntry> entries;

  /// True on the last page; the walk is then released.
  final bool done;
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
  /// and its security-scoped access). Files are not touched.
  void removeRoot(String rootId);

  /// Starts a recursive walk of [rootId] and returns a walk id for
  /// [nextWalkBatch]. Throws `root-unavailable` if access is gone.
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
  int openFd(String uri);

  /// A private directory for the app's own data (the library database):
  /// Android `filesDir`; iOS Application Support, created if needed. Never
  /// the Documents folder, so it is not shown in Files or Finder.
  String appSupportPath();

  /// iOS debug builds only: stores a bookmark for the folder at [path] as if
  /// it had been picked, so integration tests can exercise bookmark storage,
  /// resolution and walking without driving the system picker UI. Throws
  /// `unsupported` in release builds and on Android.
  StorageRoot debugAddRoot(String path);
}
