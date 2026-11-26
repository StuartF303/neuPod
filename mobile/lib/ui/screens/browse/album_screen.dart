// SPDX-License-Identifier: MPL-2.0

/// An album: the status bar's title is the album; a header with the cover,
/// the artist, the year, the song count and the total time (the specification;
/// the mock-ups drew the disc count instead of the last two, and the build
/// follows the specification); then the tracks, numbered, with a heading for
/// each disc when there are several. A track that cannot play is greyed with
/// the reason at its right. On a compilation each track shows its own artist
/// at the right. Select plays the album from that track.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/icloud_download_providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/style/backdrop.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// One album.
class AlbumScreen extends ConsumerWidget {
  /// The album with [albumId].
  const AlbumScreen({required this.albumId, super.key});

  final String albumId;

  @override
  Widget build(BuildContext context, WidgetRef ref) =>
      LibraryLoader<AlbumDetail?>(
        load: (library) => library.album(albumId),
        builder: (context, detail) {
          if (detail == null) {
            return const ShellFrame(title: '', body: SizedBox.shrink());
          }
          final album = detail.album;
          final palette = ref.watch(artPaletteProvider(album.art)).value;
          return ShellFrame(
            title: album.title,
            palette: palette,
            backdrop: BackdropLayout.album,
            body: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AlbumHeader(
                  artist: album.artist,
                  details: albumDetails(album),
                  art: CoverArt(album.art, decodeWidth: 96),
                ),
                Expanded(
                  child: BrowseListBody(
                    topRule: true,
                    child: Builder(
                      builder: (context) => NeupodList(
                        indexed: false,
                        items: _items(context, ref, detail),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      );

  List<NeupodListItem> _items(
    BuildContext context,
    WidgetRef ref,
    AlbumDetail detail,
  ) {
    final tier = NeupodTheme.of(context).tier;
    final songs = detail.songs;
    final downloading = ref.watch(icloudDownloadsProvider);
    return [
      for (final disc in detail.discs) ...[
        if (detail.hasDiscHeadings && disc.number != null)
          bandItem(
            height: 34 * tier.k,
            child: DiscHeading('Disc ${disc.number}'),
          ),
        for (final song in disc.songs)
          NeupodListItem.row(
            height: tier.rowHeight,
            title: song.title,
            number: song.track?.toString(),
            value: detail.album.compilation ? song.artist : null,
            disabledReason: songRowReason(song, downloading),
            greyedAction: song.downloadable ? downloadActionText : null,
            onActivate: () => playFrom(ref, songs, song),
            id: song.id,
          ),
      ],
    ];
  }
}
