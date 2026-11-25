// SPDX-License-Identifier: MPL-2.0

/// Albums, A to Z by title, with the fast-scroll letter; each row is the
/// cover, the title, and `Artist · Year`. `AlbumListScreen` also lists the
/// Compilations.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';

/// A list of albums from [load], titled [title] (Albums, Compilations).
class AlbumListScreen extends ConsumerWidget {
  /// An album list.
  const AlbumListScreen({
    required this.title,
    required this.load,
    required this.subtitle,
    super.key,
  });

  final String title;
  final Future<List<Album>> Function(LibraryView library) load;
  final String Function(Album album) subtitle;

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<List<Album>>(
        load: load,
        builder: (context, albums) => ShellFrame(
          title: title,
          context: albums == null ? null : groupedCount(albums.length),
          body: albums == null
              ? const SizedBox.shrink()
              : BrowseListBody(
                  child: Builder(
                    builder: (context) => NeupodList(
                      items: [
                        for (final a in albums)
                          albumItem(
                            context,
                            album: a,
                            subtitle: subtitle(a),
                            onActivate: () => pushScreen(
                              ref,
                              a.title,
                              (_) => AlbumScreen(albumId: a.id),
                            ),
                          ),
                      ],
                    ),
                  ),
                ),
        ),
      );
}

/// Albums.
class AlbumsScreen extends StatelessWidget {
  /// Every album, by title.
  const AlbumsScreen({super.key});

  @override
  Widget build(BuildContext context) => AlbumListScreen(
    title: 'Albums',
    load: (library) => library.albums(),
    subtitle: albumArtistYear,
  );
}
