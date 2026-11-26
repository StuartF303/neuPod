// SPDX-License-Identifier: MPL-2.0

/// The app's real scanner over a plain folder on the host: a
/// [NeupodStorageApi] that walks a directory with dart:io and hands out
/// `file://` entries, so the scanner reads them through the same
/// `FileReaderHandle` path as on iOS. For the spec suites and the scale test,
/// which need the whole data layer without a phone.
library;

import 'dart:io';

import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod_storage/neupod_storage.dart';
// ignore: implementation_imports
import 'package:neupod_storage/src/messages.g.dart';

import '../fakes/fake_folder_access.dart';

/// Serves each directory of [roots] (by folder id) as a storage root. Every
/// file is local (downloaded); walks are served in pages of [pageSize], as
/// the platforms do.
final class HostStorageApi extends NeupodStorageApi {
  HostStorageApi(this.roots, {this.pageSize = 500});

  final Map<String, Directory> roots;
  final int pageSize;
  final Map<String, List<StorageEntry>> _walks = {};
  int _n = 0;

  @override
  Future<String> startWalk(String rootId) async {
    final dir = roots[rootId];
    if (dir == null) throw Exception('root-unavailable');
    final entries = <StorageEntry>[
      for (final f in dir.listSync(recursive: true).whereType<File>())
        // Hidden names, files and folders, are skipped by both platforms'
        // walks (NeupodStoragePlugin.swift and .kt), as by the firmware's.
        if (!f.path
            .substring(dir.path.length + 1)
            .split('/')
            .any((s) => s.startsWith('.')))
          StorageEntry(
            relPath: f.path.substring(dir.path.length + 1),
            uri: f.uri.toString(),
            size: f.lengthSync(),
            modifiedMs: f.lastModifiedSync().millisecondsSinceEpoch,
            state: EntryState.local,
          ),
    ]..sort((a, b) => a.relPath.compareTo(b.relPath));
    final id = 'w${_n++}';
    _walks[id] = entries;
    return id;
  }

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async {
    final rest = _walks[walkId]!;
    final n = rest.length < pageSize ? rest.length : pageSize;
    final page = rest.sublist(0, n);
    rest.removeRange(0, n);
    final done = rest.isEmpty;
    if (done) _walks.remove(walkId);
    return WalkBatch(entries: page, done: done, complete: done);
  }

  @override
  Future<void> cancelWalk(String walkId) async => _walks.remove(walkId);
}

/// Scans [root] as the one picked folder `Music` into [db] with the app's
/// [LocalLibraryScanner], to the end. Returns the last progress reported.
Future<ScanProgress?> scanHostFolder(
  LibraryDatabase db,
  Directory root, {
  int version = scanVersion,
}) async {
  final scanner = LocalLibraryScanner(
    folders: FakeFolderAccess([
      const LibraryFolder(
        id: 'music',
        name: 'Music',
        kind: FolderKind.pickedFolder,
        available: true,
      ),
    ]),
    storage: NeupodStorage(api: HostStorageApi({'music': root})),
    db: db,
    version: version,
  );
  ScanProgress? last;
  await for (final p in scanner.scan()) {
    last = p;
  }
  return last;
}
