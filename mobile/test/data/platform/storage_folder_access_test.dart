// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod_storage/neupod_storage.dart';
import 'package:neupod_storage/src/messages.g.dart';

class FakeApi extends NeupodStorageApi {
  final removed = <String>[];
  StorageRoot? pick;
  PlatformException? pickError;
  StorageRoot? collection;
  int collectionAsks = 0;

  @override
  Future<StorageRoot?> addSharedCollection() async {
    collectionAsks++;
    return collection;
  }

  @override
  Future<List<StorageRoot>> listRoots() async => [
    StorageRoot(
      id: 'documents',
      displayName: 'neuPod',
      kind: RootKind.appDocuments,
      available: true,
    ),
    StorageRoot(
      id: 'b1',
      displayName: 'Music',
      kind: RootKind.pickedFolder,
      available: false,
    ),
  ];

  @override
  Future<StorageRoot?> pickFolder() async {
    if (pickError != null) throw pickError!;
    return pick;
  }

  @override
  Future<void> removeRoot(String rootId) async => removed.add(rootId);
}

void main() {
  test('maps roots to library folders', () async {
    final access = StorageFolderAccess(NeupodStorage(api: FakeApi()));
    expect(await access.folders(), const [
      LibraryFolder(
        id: 'documents',
        name: 'neuPod',
        kind: FolderKind.appDocuments,
        available: true,
      ),
      LibraryFolder(
        id: 'b1',
        name: 'Music',
        kind: FolderKind.pickedFolder,
        available: false,
      ),
    ]);
  });

  test('pick: cancel is null, a pick is mapped, errors are typed', () async {
    final api = FakeApi();
    final access = StorageFolderAccess(NeupodStorage(api: api));
    expect(await access.pickFolder(), isNull);

    api.pick = StorageRoot(
      id: 'content://t',
      displayName: 'Music',
      kind: RootKind.pickedFolder,
      available: true,
    );
    expect((await access.pickFolder())?.id, 'content://t');

    api.pickError = PlatformException(code: 'busy');
    await expectLater(
      access.pickFolder(),
      throwsA(
        isA<FolderAccessException>().having((e) => e.code, 'code', 'busy'),
      ),
    );
  });

  test('the shared collection: refused is null, on is mapped, iOS does not '
      'offer it', () async {
    final api = FakeApi();
    final storage = NeupodStorage(api: api);
    expect(StorageFolderAccess(storage).offersSharedCollection, isFalse);
    final access = StorageFolderAccess(storage, offersSharedCollection: true);
    expect(access.offersSharedCollection, isTrue);
    expect(await access.addSharedCollection(), isNull);
    api.collection = StorageRoot(
      id: sharedCollectionRootId,
      displayName: 'Music on This Phone',
      kind: RootKind.sharedCollection,
      available: true,
    );
    expect(
      await access.addSharedCollection(),
      const LibraryFolder(
        id: 'mediastore:audio',
        name: 'Music on This Phone',
        kind: FolderKind.sharedCollection,
        available: true,
      ),
    );
    expect(api.collectionAsks, 2);
  });

  test('remove forwards the id', () async {
    final api = FakeApi();
    await StorageFolderAccess(NeupodStorage(api: api)).removeFolder('b1');
    expect(api.removed, ['b1']);
  });
}
