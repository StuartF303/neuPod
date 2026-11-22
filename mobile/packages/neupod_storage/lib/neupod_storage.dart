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
    show EntryState, RootKind, StorageEntry, StorageRoot, WalkBatch;
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
  /// and so on, as the platform side reports them.
  final String code;
  final String? message;

  @override
  String toString() =>
      'StorageException($code${message == null ? '' : ': $message'})';
}

/// The entry point. One instance per app is enough; it holds no state of its
/// own (the platform side holds the grants and bookmarks).
class NeupodStorage {
  NeupodStorage({NeupodStorageApi? api}) : _api = api ?? NeupodStorageApi();

  final NeupodStorageApi _api;

  /// Shows the system folder picker. Returns the root, or null if cancelled.
  Future<StorageRoot?> pickFolder() => _guard(_api.pickFolder);

  /// The roots the app holds. On iOS the Documents folder comes first.
  Future<List<StorageRoot>> roots() => _guard(_api.listRoots);

  /// Releases a picked root. The files are not touched.
  Future<void> removeRoot(String rootId) =>
      _guard(() => _api.removeRoot(rootId));

  /// Walks [rootId] recursively, yielding files in pages of about
  /// [batchSize]. Cancelling the subscription stops the walk.
  Stream<List<StorageEntry>> walk(String rootId, {int batchSize = 500}) async* {
    final walkId = await _guard(() => _api.startWalk(rootId));
    var done = false;
    try {
      while (!done) {
        final batch = await _guard(() => _api.nextWalkBatch(walkId, batchSize));
        done = batch.done;
        if (batch.entries.isNotEmpty) yield batch.entries;
      }
    } finally {
      if (!done) {
        // Best effort: a cancelled walk is dropped on the platform side.
        unawaited(_api.cancelWalk(walkId).catchError((Object _) {}));
      }
    }
  }

  /// Opens [uri] (from a [StorageEntry]) for random-access reads.
  ///
  /// `file://` URIs (iOS) are read with `dart:io`; `content://` URIs
  /// (Android) through a detached file descriptor. This call goes through a
  /// platform channel on Android, so make it on the root isolate; to read in
  /// a background isolate, use [openHandle] and send the handle there.
  Future<StorageReader> openReader(String uri) async =>
      (await openHandle(uri)).open();

  /// Opens [uri] and returns a sendable [ReaderHandle] for another isolate.
  Future<ReaderHandle> openHandle(String uri) async {
    if (uri.startsWith('file:')) return FileReaderHandle(uri);
    return FdReaderHandle(await _guard(() => _api.openFd(uri)));
  }

  /// iOS debug builds only: adds the folder at [path] as if it had been
  /// picked. For integration tests; never called by the app.
  Future<StorageRoot> debugAddRoot(String path) =>
      _guard(() => _api.debugAddRoot(path));

  static Future<T> _guard<T>(Future<T> Function() call) async {
    try {
      return await call();
    } on PlatformException catch (e) {
      throw StorageException(e.code, e.message);
    }
  }
}
