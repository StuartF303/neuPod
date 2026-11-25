// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// [FolderAccess] over the in-repo neupod_storage plugin (decision 0004).
final class StorageFolderAccess implements FolderAccess {
  StorageFolderAccess(this._storage, {this.offersSharedCollection = false});

  final NeupodStorage _storage;

  @override
  final bool offersSharedCollection;

  @override
  Future<LibraryFolder?> addSharedCollection() => _guard(() async {
    final root = await _storage.addSharedCollection();
    return root == null ? null : _folder(root);
  });

  @override
  Future<List<LibraryFolder>> folders() =>
      _guard(() async => (await _storage.roots()).map(_folder).toList());

  @override
  Future<LibraryFolder?> pickFolder() => _guard(() async {
    final root = await _storage.pickFolder();
    return root == null ? null : _folder(root);
  });

  @override
  Future<void> removeFolder(String id) => _guard(() => _storage.removeRoot(id));

  static LibraryFolder _folder(StorageRoot root) => LibraryFolder(
    id: root.id,
    name: root.displayName,
    kind: switch (root.kind) {
      RootKind.appDocuments => FolderKind.appDocuments,
      RootKind.pickedFolder => FolderKind.pickedFolder,
      RootKind.sharedCollection => FolderKind.sharedCollection,
    },
    available: root.available,
  );

  static Future<T> _guard<T>(Future<T> Function() call) async {
    try {
      return await call();
    } on StorageException catch (e) {
      throw FolderAccessException(e.code, e.message);
    }
  }
}
