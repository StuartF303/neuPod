// SPDX-License-Identifier: MPL-2.0

/// Songs: two lines per row (title over `Artist · Album`), the count in the
/// status bar, the fast-scroll letter on the title. Select plays the song
/// and queues the list it was chosen from. The same screen lists an artist's
/// or a genre's All Songs. Every song, when there are none, points to
/// Settings > Library.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// Every song.
class SongsScreen extends StatelessWidget {
  /// Every song, by title.
  const SongsScreen({super.key});

  @override
  Widget build(BuildContext context) => SongListScreen(
    title: 'Songs',
    load: (library) => library.songs(),
    emptyHint: true,
  );
}

/// A list of songs from [load]. [title] is the status bar's.
class SongListScreen extends ConsumerWidget {
  /// A song list.
  const SongListScreen({
    required this.title,
    required this.load,
    this.emptyHint = false,
    super.key,
  });

  final String title;
  final Future<List<Song>> Function(LibraryView library) load;

  /// An empty list says `No songs yet` and opens Settings > Library (the
  /// firmware's empty heading, with the app's place to add music), rather
  /// than `None yet`.
  final bool emptyHint;

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<List<Song>>(
        load: load,
        builder: (context, songs) => ShellFrame(
          title: title,
          context: songs == null ? null : groupedCount(songs.length),
          body: songs == null
              ? const SizedBox.shrink()
              : BrowseListBody(
                  child: Builder(
                    builder: (context) => NeupodList(
                      items: [
                        if (songs.isEmpty && emptyHint)
                          noSongsItem(context, ref),
                        for (final song in songs)
                          songItem(
                            context,
                            song: song,
                            onActivate: () => playFrom(ref, songs, song),
                          ),
                      ],
                    ),
                  ),
                ),
        ),
      );
}

/// `No songs yet` over `Settings > Library`; Select opens Library.
NeupodListItem noSongsItem(BuildContext context, WidgetRef ref) =>
    NeupodListItem.row(
      height: NeupodTheme.of(context).tier.row2Height,
      title: noSongsTitle,
      subtitle: addMusicHint,
      chevron: true,
      onActivate: () =>
          pushScreen(ref, 'Library', (_) => const LibraryScreen()),
    );
