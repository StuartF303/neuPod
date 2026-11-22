// SPDX-License-Identifier: MPL-2.0

/// Settings > Library: the folders the library is read from (add, remove),
/// Update Library, and the scan's progress, as a plain wheel list.
///
/// Not drawn by any design: the layout is this build's simplest reading of
/// the specification's "library folders (add, remove, rescan)", shown for the
/// owner to judge. The wording follows the firmware where it has one
/// ("Update Library", "Building library", "Select: cancel", "Library update
/// stopped").
library;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/ui/features/library_setup/library_setup_providers.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// The firmware's heading of an empty Songs list (`ui_library.c`).
const noSongsTitle = 'No songs yet';

/// Where music is added, as the firmware points to its own place
/// (`Music > Update Library` in `ui_playlists.c`).
const addMusicHint = 'Settings > Library';

/// The ellipsis the firmware's labels use.
const _ellipsis = '…';

/// What the user needs to know about where music can come from. Owner
/// decision 8: Android 11+ refuses the storage root and Download.
String libraryHintFor(TargetPlatform platform) => switch (platform) {
  TargetPlatform.iOS =>
    'Copy music into On My iPhone > neuPod in the Files app, or drag '
        'folders onto neuPod in Finder when the phone is plugged in. You can '
        'also add a folder from iCloud Drive or an attached drive.',
  _ =>
    'Android does not let apps read the top of the phone\'s storage, an SD '
        'card\'s top folder, or Download. Choose Music, or a folder inside '
        'it.',
};

/// What the panel says when adding a folder failed with [code].
String addFolderMessage(String code, TargetPlatform platform) => switch (code) {
  'busy' => 'The folder picker is already open.',
  'not-persistable' => libraryHintFor(platform),
  _ => "neuPod couldn't keep access to that folder ($code).",
};

/// The progress line of a running scan: `120 found · 45 read`.
String scanProgressLabel(ScanProgress p) =>
    '${groupedCount(p.filesFound)} found · ${groupedCount(p.filesRead)} read';

/// Settings > Library.
class LibraryScreen extends ConsumerWidget {
  /// Settings > Library.
  const LibraryScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // A scan that stops early (cancelled, or failed) says so in a panel:
    // songs may be missing (UI styles design, the messages that move to the
    // panel).
    ref.listen(libraryScanProvider, (was, now) {
      final ended = was != null && !was.done && now != null && now.done;
      if (ended && now.error != null) {
        ref
            .read(shellProvider.notifier)
            .showPanelRequest(const PanelRequest.libraryUpdateStopped());
      }
    });
    final folders = ref.watch(libraryFoldersProvider);
    final scan = ref.watch(libraryScanProvider);
    final scanning = scan != null && !scan.done;
    return ShellFrame(
      title: 'Library',
      body: BrowseListBody(
        child: Builder(
          builder: (context) {
            final tier = NeupodTheme.of(context).tier;
            final h = tier.rowHeight;
            final list = folders.value;
            return NeupodList(
              indexed: false,
              items: [
                if (list != null)
                  for (final f in list) _folderItem(context, ref, f),
                NeupodListItem.row(
                  height: h,
                  title: 'Add Folder$_ellipsis',
                  onActivate: () => _add(ref),
                ),
                if (scanning)
                  NeupodListItem.row(
                    height: tier.row2Height,
                    title: 'Building library',
                    subtitle: '${scanProgressLabel(scan)} · Select: cancel',
                    onActivate: () =>
                        ref.read(libraryScanProvider.notifier).cancel(),
                  )
                else
                  NeupodListItem.row(
                    height: h,
                    title: 'Update Library',
                    onActivate: () =>
                        ref.read(libraryScanProvider.notifier).start(),
                  ),
              ],
            );
          },
        ),
      ),
    );
  }

  NeupodListItem _folderItem(
    BuildContext context,
    WidgetRef ref,
    LibraryFolder f,
  ) {
    final tier = NeupodTheme.of(context).tier;
    final second = [
      if (f.kind == FolderKind.appDocuments) 'On My iPhone',
      if (!f.available) 'Not available',
    ].join(' · ');
    return NeupodListItem.row(
      height: second.isEmpty ? tier.rowHeight : tier.row2Height,
      title: f.name,
      subtitle: second.isEmpty ? null : second,
      chevron: f.removable,
      onActivate: f.removable
          ? () => pushScreen(ref, f.name, (_) => FolderScreen(folder: f))
          // The Documents folder cannot be removed: say how to fill it.
          : () => ref
                .read(shellProvider.notifier)
                .showPanel(
                  title: 'On My iPhone',
                  message: libraryHintFor(TargetPlatform.iOS),
                ),
    );
  }

  Future<void> _add(WidgetRef ref) async {
    final shell = ref.read(shellProvider.notifier);
    try {
      // A folder added starts a scan (LibraryFoldersNotifier).
      await ref.read(libraryFoldersProvider.notifier).add();
    } on FolderAccessException catch (e) {
      await shell.showPanel(
        title: "Can't add folder",
        message: addFolderMessage(e.code, defaultTargetPlatform),
      );
    }
  }
}

/// A picked folder: Remove (which goes back to Library). Menu keeps it.
class FolderScreen extends ConsumerWidget {
  /// The screen of [folder].
  const FolderScreen({required this.folder, super.key});

  final LibraryFolder folder;

  @override
  Widget build(BuildContext context, WidgetRef ref) => ShellFrame(
    title: folder.name,
    body: BrowseListBody(
      child: Builder(
        builder: (context) => NeupodList(
          indexed: false,
          items: [
            NeupodListItem.row(
              height: NeupodTheme.of(context).tier.rowHeight,
              title: 'Remove',
              onActivate: () => _remove(ref),
            ),
          ],
        ),
      ),
    ),
  );

  Future<void> _remove(WidgetRef ref) async {
    final shell = ref.read(shellProvider.notifier);
    try {
      // The scan that follows removes the folder's songs.
      await ref.read(libraryFoldersProvider.notifier).remove(folder.id);
      shell.pop();
    } on FolderAccessException catch (e) {
      await shell.showPanel(
        title: "Can't remove folder",
        message: "neuPod couldn't give up that folder (${e.code}).",
      );
    }
  }
}
