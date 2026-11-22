// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_folder.dart';

/// The library folders, with add and remove. Forwards to [FolderAccess]; it
/// holds no rules of its own (decision 0006).
final libraryFoldersProvider =
    AsyncNotifierProvider<LibraryFoldersNotifier, List<LibraryFolder>>(
      LibraryFoldersNotifier.new,
    );

class LibraryFoldersNotifier extends AsyncNotifier<List<LibraryFolder>> {
  FolderAccess get _access => ref.read(folderAccessProvider);

  @override
  Future<List<LibraryFolder>> build() =>
      ref.watch(folderAccessProvider).folders();

  /// Shows the picker; returns the picked folder, or null if cancelled.
  /// Throws [FolderAccessException] for the screen to show.
  Future<LibraryFolder?> add() async {
    final folder = await _access.pickFolder();
    if (folder != null) state = AsyncData(await _access.folders());
    return folder;
  }

  Future<void> remove(String id) async {
    await _access.removeFolder(id);
    state = AsyncData(await _access.folders());
  }
}
