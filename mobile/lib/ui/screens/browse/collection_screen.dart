// SPDX-License-Identifier: MPL-2.0

/// An artist or a genre: **All Songs** first, then its albums as two-line
/// rows with a cover thumbnail, in the order the library view gives (an
/// artist's by year, a genre's by artist then year).
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// All Songs and the albums of an artist or genre.
class CollectionScreen extends ConsumerWidget {
  const CollectionScreen._({
    required this.title,
    required this.albums,
    required this.songs,
    required this.subtitle,
  });

  /// An artist's screen: their albums (year order) as `1993 · 10 songs`.
  factory CollectionScreen.artist(Artist artist) => CollectionScreen._(
    title: artist.name,
    albums: (library) => library.albumsOfArtist(artist.id),
    songs: (library) => library.songsOfArtist(artist.id),
    subtitle: (a) => albumDetails(a, length: false).join(' · '),
  );

  /// A genre's screen: its albums as `Artist · 1993`.
  factory CollectionScreen.genre(Genre genre) => CollectionScreen._(
    title: genre.name,
    albums: (library) => library.albumsOfGenre(genre.id),
    songs: (library) => library.songsOfGenre(genre.id),
    subtitle: (a) => [a.artist, if (a.year != null) '${a.year}'].join(' · '),
  );

  final String title;
  final Future<List<Album>> Function(LibraryView library) albums;
  final Future<List<Song>> Function(LibraryView library) songs;
  final String Function(Album album) subtitle;

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<List<Album>>(
        load: albums,
        builder: (context, list) => ShellFrame(
          title: title,
          context: list == null ? null : albumsLabel(list.length),
          body: list == null
              ? const SizedBox.shrink()
              : BrowseListBody(
                  child: Builder(
                    builder: (context) {
                      final h = NeupodTheme.of(context).tier.rowHeight;
                      return NeupodList(
                        indexed: false,
                        items: [
                          NeupodListItem.row(
                            height: h,
                            title: 'All Songs',
                            chevron: true,
                            onActivate: () => pushScreen(
                              ref,
                              'All Songs',
                              (_) => SongListScreen(
                                title: 'All Songs',
                                load: songs,
                              ),
                            ),
                          ),
                          for (final a in list)
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
                      );
                    },
                  ),
                ),
        ),
      );
}
