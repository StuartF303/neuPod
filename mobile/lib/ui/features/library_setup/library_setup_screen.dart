// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/ui/features/library_setup/library_setup_providers.dart';

/// Plain "Folders" screen (phase 1): the folders the library is read from,
/// Add folder, and Remove. Phase 2 restyles it.
class LibrarySetupScreen extends ConsumerWidget {
  const LibrarySetupScreen({super.key});

  /// What the user needs to know about where music can come from. Owner
  /// decision 8: Android 11+ refuses the storage root and Download.
  static String hintFor(TargetPlatform platform) => switch (platform) {
    TargetPlatform.iOS =>
      'Copy music into On My iPhone > neuPod in the Files app, or drag '
          'folders onto neuPod in Finder when the phone is plugged in. You can '
          'also add a folder from iCloud Drive or an attached drive.',
    _ =>
      'Android does not let apps read the top of the phone\'s storage, an SD '
          'card\'s top folder, or Download. Choose Music, or a folder inside '
          'it.',
  };

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final folders = ref.watch(libraryFoldersProvider);
    return Scaffold(
      appBar: AppBar(title: const Text('Folders')),
      body: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Expanded(
              child: switch (folders) {
                AsyncData(:final value) when value.isEmpty => const Center(
                  child: Text('No folders yet'),
                ),
                AsyncData(:final value) => ListView(
                  children: [for (final f in value) _FolderRow(folder: f)],
                ),
                AsyncError(:final error) => Center(
                  child: Text('Could not list folders: $error'),
                ),
                _ => const Center(child: CircularProgressIndicator()),
              },
            ),
            Padding(
              padding: const EdgeInsets.all(16),
              child: Text(hintFor(defaultTargetPlatform)),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
              child: FilledButton(
                onPressed: () => _add(context, ref),
                child: const Text('Add folder'),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _add(BuildContext context, WidgetRef ref) async {
    final messenger = ScaffoldMessenger.of(context);
    try {
      await ref.read(libraryFoldersProvider.notifier).add();
    } on FolderAccessException catch (e) {
      messenger.showSnackBar(
        SnackBar(content: Text('Could not add the folder (${e.code})')),
      );
    }
  }
}

class _FolderRow extends ConsumerWidget {
  const _FolderRow({required this.folder});

  final LibraryFolder folder;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final subtitle = [
      if (folder.kind == FolderKind.appDocuments) 'On My iPhone',
      if (!folder.available) 'Not available',
    ].join(' · ');
    return ListTile(
      title: Text(folder.name),
      subtitle: subtitle.isEmpty ? null : Text(subtitle),
      trailing: folder.removable
          ? TextButton(
              onPressed: () =>
                  ref.read(libraryFoldersProvider.notifier).remove(folder.id),
              child: const Text('Remove'),
            )
          : null,
    );
  }
}
