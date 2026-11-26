// SPDX-License-Identifier: MPL-2.0

/// Read-only folder access for the neuPod app (decision 0004): Android
/// Storage Access Framework trees and iOS Documents plus security-scoped
/// bookmarks. There is no call that writes, moves or deletes a file.
library;

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:neupod_storage/src/messages.g.dart';
import 'package:neupod_storage/src/readers.dart';

export 'package:neupod_storage/src/messages.g.dart'
    show
        DownloadState,
        EntryState,
        RootKind,
        StorageEntry,
        StorageRoot,
        WalkBatch;
export 'package:neupod_storage/src/readers.dart'
    show
        FdReaderHandle,
        FdStorageReader,
        FileReaderHandle,
        FileStorageReader,
        ReaderHandle,
        StorageReader;

/// Thrown for a storage failure the caller can act on.
class StorageException implements Exception {
  StorageException(this.code, [this.message]);

  /// `root-unavailable`, `busy`, `open-failed`, `not-removable`, `unsupported`
  /// and so on, as the platform side reports them; `timeout` for a walk page
  /// that did not come back in time (the Dart side).
  final String code;
  final String? message;

  @override
  String toString() =>
      'StorageException($code${message == null ? '' : ': $message'})';
}

/// How long [NeupodStorage.walk] waits for one page by default. A page is
/// about 500 files' metadata: well under a second on a phone, so this only
/// trips when something is stuck.
const walkPageTimeout = Duration(minutes: 2);

/// How long [NeupodStorage.roots] waits by default.
const rootsTimeout = Duration(seconds: 30);

/// The entry point. One instance per app is enough; it holds no state of its
/// own (the platform side holds the grants and bookmarks).
class NeupodStorage {
  NeupodStorage({NeupodStorageApi? api}) : _api = api ?? NeupodStorageApi();

  final NeupodStorageApi _api;

  /// Shows the system folder picker. Returns the root, or null if cancelled.
  Future<StorageRoot?> pickFolder() => _guard(_api.pickFolder);

  /// The roots the app holds. On iOS the Documents folder comes first.
  ///
  /// Ends with `StorageException('timeout')` after [timeout]: resolving a
  /// bookmark or checking a root can wait on a drive or a provider.
  Future<List<StorageRoot>> roots({Duration timeout = rootsTimeout}) =>
      _guard(() => _timed(_api.listRoots(), timeout, 'the roots'));

  /// Releases a picked root. The files are not touched.
  Future<void> removeRoot(String rootId) =>
      _guard(() => _api.removeRoot(rootId));

  /// Walks [rootId] recursively, yielding pages of about [batchSize] files.
  /// The last page has `done` set, and `complete` tells whether every folder
  /// was listed: files missing from an incomplete walk are not known to be
  /// gone. Cancelling the subscription stops the walk.
  ///
  /// A page that takes longer than [pageTimeout] ends the walk with
  /// `StorageException('timeout')`, so a provider that never answers cannot
  /// hang a scan (the platform side also gives up on a coordinated read that
  /// does not start; iOS never reads a file that is not downloaded).
  Stream<WalkBatch> walk(
    String rootId, {
    int batchSize = 500,
    Duration pageTimeout = walkPageTimeout,
  }) async* {
    final walkId = await _guard(
      () => _timed(_api.startWalk(rootId), pageTimeout, 'a walk of $rootId'),
    );
    var done = false;
    try {
      while (!done) {
        final batch = await _guard(
          () => _timed(
            _api.nextWalkBatch(walkId, batchSize),
            pageTimeout,
            'a page of $rootId',
          ),
        );
        done = batch.done;
        yield batch;
      }
    } finally {
      if (!done) {
        // Best effort: a cancelled walk is dropped on the platform side.
        unawaited(_api.cancelWalk(walkId).catchError((Object _) {}));
      }
    }
  }

  /// Opens [entry] for random-access reads.
  ///
  /// `file://` URIs (iOS) are read with `dart:io`; `content://` URIs
  /// (Android) through a detached file descriptor. This call goes through a
  /// platform channel on Android, so make it on the root isolate; to read in
  /// a background isolate, use [openHandle] and send the handle there.
  Future<StorageReader> openReader(StorageEntry entry) async =>
      (await openHandle(entry)).open();

  /// Opens [entry] and returns a sendable [ReaderHandle] for another isolate.
  ///
  /// Refuses an entry that is not downloaded (`not-downloaded`): reading it
  /// would make iOS fetch it from iCloud (owner decision 7).
  Future<ReaderHandle> openHandle(StorageEntry entry) async {
    if (entry.state == EntryState.notDownloaded) {
      throw StorageException('not-downloaded', entry.relPath);
    }
    final uri = entry.uri;
    if (uri.startsWith('file:')) return FileReaderHandle(uri);
    return FdReaderHandle(await _guard(() => _api.openFd(uri)));
  }

  /// iOS only: asks iOS to download the iCloud file at [uri], the URI of an
  /// entry a walk reported `notDownloaded` (owner decision 7). Returns once iOS has the request; the
  /// file arrives later, and a walk then reports it `local`. The app reads
  /// nothing of it here. Throws `download-failed` when iOS refuses,
  /// `not-in-root` for a file outside the held folders, and `unsupported` on
  /// Android.
  Future<void> requestDownload(String uri) =>
      _guard(() => _api.requestDownload(uri));

  /// iOS only: where the download of each of [uris] stands, in order, from
  /// file metadata alone. Throws `unsupported` on Android.
  Future<List<DownloadState>> downloadStates(List<String> uris) =>
      _guard(() => _api.downloadStates(uris));

  /// A private directory for the app's own data, such as the library
  /// database (never the user-visible Documents folder).
  Future<String> appSupportPath() => _guard(_api.appSupportPath);

  /// A private directory for the app's data that phone backups keep (the
  /// ratings and play counts database; owner decision 20).
  Future<String> backedUpDataPath() => _guard(_api.backedUpDataPath);

  /// iOS only, for tests: whether [path] is excluded from backups.
  Future<bool> isExcludedFromBackup(String path) =>
      _guard(() => _api.isExcludedFromBackup(path));

  /// iOS debug builds only: adds the folder at [path] as if it had been
  /// picked. For integration tests; never called by the app.
  Future<StorageRoot> debugAddRoot(String path) =>
      _guard(() => _api.debugAddRoot(path));

  static Future<T> _timed<T>(Future<T> call, Duration limit, String what) =>
      call.timeout(
        limit,
        onTimeout: () =>
            throw StorageException('timeout', 'no $what in $limit'),
      );

  static Future<T> _guard<T>(Future<T> Function() call) async {
    try {
      return await call();
    } on PlatformException catch (e) {
      throw StorageException(e.code, e.message);
    }
  }
}
