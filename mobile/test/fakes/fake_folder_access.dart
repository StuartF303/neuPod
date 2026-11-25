// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/library_folder.dart';

/// An in-memory [FolderAccess]. [nextPick] is what the "picker" returns next;
/// [pickError] makes it throw instead.
class FakeFolderAccess implements FolderAccess {
  FakeFolderAccess([List<LibraryFolder>? initial]) : held = [...?initial];

  /// Android: the shared collection is offered.
  @override
  bool offersSharedCollection = false;

  /// What turning the shared collection on gives: the folder, or null as a
  /// refused permission.
  LibraryFolder? sharedCollection;
  int sharedCollectionAsks = 0;

  @override
  Future<LibraryFolder?> addSharedCollection() async {
    sharedCollectionAsks++;
    final c = sharedCollection;
    if (c != null && !held.any((f) => f.id == c.id)) held.add(c);
    return c;
  }

  final List<LibraryFolder> held;
  LibraryFolder? nextPick;
  FolderAccessException? pickError;
  FolderAccessException? removeError;
  final removed = <String>[];

  @override
  Future<List<LibraryFolder>> folders() async => List.of(held);

  @override
  Future<LibraryFolder?> pickFolder() async {
    if (pickError != null) throw pickError!;
    final pick = nextPick;
    if (pick != null && !held.any((f) => f.id == pick.id)) held.add(pick);
    return pick;
  }

  @override
  Future<void> removeFolder(String id) async {
    if (removeError != null) throw removeError!;
    removed.add(id);
    held.removeWhere((f) => f.id == id);
  }
}
