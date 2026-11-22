// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_folder.dart';

/// The library folders, with add and remove. Forwards to [FolderAccess]; it
/// holds no rules of its own (decision 0006). A change of folders starts a
/// scan (restarting one that runs), so the library follows the folders.
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
    if (folder != null) {
      state = AsyncData(await _access.folders());
      _rescan();
    }
    return folder;
  }

  /// Gives up a picked folder; the scan that follows removes its songs.
  Future<void> remove(String id) async {
    await _access.removeFolder(id);
    state = AsyncData(await _access.folders());
    _rescan();
  }

  void _rescan() =>
      unawaited(ref.read(libraryScanProvider.notifier).start(restart: true));
}
