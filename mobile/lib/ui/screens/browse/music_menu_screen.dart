// SPDX-License-Identifier: MPL-2.0

/// The Music menu: Cover Flow, Playlists, Artists, Albums, Songs, Genres,
/// Compilations and, as the last item, Search (owner decision 12; its
/// placement is device view Open question 11). The right pane previews the
/// highlighted item: four covers for the lists, one for Cover Flow.
///
/// The design draws the pane for Albums only; the other previews and their
/// second lines are this build's reading of it, shown at the gate.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/components/art_frame.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/neupod_screen.dart';
import 'package:neupod/ui/screens/browse/albums_screen.dart';
import 'package:neupod/ui/screens/browse/artists_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/compilations_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';
import 'package:neupod/ui/screens/browse/genres_screen.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/shell/stand_in_screen.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The Music menu's items, in order.
enum MusicItem {
  coverFlow('Cover Flow', 'A to Z'),
  playlists('Playlists', 'None yet'),
  artists('Artists', 'A to Z'),
  albums('Albums', 'A to Z'),
  songs('Songs', 'A to Z'),
  genres('Genres', 'By style'),
  compilations('Compilations', 'Various artists'),
  search('Search', 'Type with the wheel');

  const MusicItem(this.label, this.caption);

  final String label;
  final String caption;
}

/// The Music menu.
class MusicMenuScreen extends ConsumerStatefulWidget {
  /// The menu.
  const MusicMenuScreen({super.key});

  @override
  ConsumerState<MusicMenuScreen> createState() => _MusicMenuScreenState();
}

class _MusicMenuScreenState extends ConsumerState<MusicMenuScreen> {
  int _sel = 0;

  void _open(MusicItem item) {
    switch (item) {
      case MusicItem.coverFlow:
        pushScreen(ref, 'Cover Flow', (_) => const CoverFlowScreen());
      case MusicItem.playlists:
        pushScreen(ref, 'Playlists', (_) => const StandInScreen('Playlists'));
      case MusicItem.artists:
        pushScreen(ref, 'Artists', (_) => const ArtistsScreen());
      case MusicItem.albums:
        pushScreen(ref, 'Albums', (_) => const AlbumsScreen());
      case MusicItem.songs:
        pushScreen(ref, 'Songs', (_) => const SongsScreen());
      case MusicItem.genres:
        pushScreen(ref, 'Genres', (_) => const GenresScreen());
      case MusicItem.compilations:
        pushScreen(ref, 'Compilations', (_) => const CompilationsScreen());
      case MusicItem.search:
        ref.read(shellProvider.notifier).open(ShellDestination.search);
    }
  }

  @override
  Widget build(BuildContext context) {
    return ShellFrame(
      title: 'Music',
      body: Builder(
        builder: (context) {
          final h = NeupodTheme.of(context).tier.rowHeight;
          final items = [
            for (final item in MusicItem.values)
              NeupodListItem.row(
                height: h,
                title: item.label,
                chevron: true,
                onActivate: () => _open(item),
              ),
          ];
          return Row(
            children: [
              SizedBox(
                width: listBesidePaneWidth,
                child: BrowseListBody(
                  besidePane: true,
                  child: NeupodList(
                    indexed: false,
                    items: items,
                    onSelectionChanged: (i) => setState(() => _sel = i),
                  ),
                ),
              ),
              Expanded(child: _MenuPane(MusicItem.values[_sel])),
            ],
          );
        },
      ),
    );
  }
}

class _MenuPane extends ConsumerWidget {
  const _MenuPane(this.item);

  final MusicItem item;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final theme = NeupodTheme.of(context);
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;
    final covers = ref.watch(_coversProvider(item)).value ?? const <Album>[];

    // 220 square in all, as the pane is 264 wide with 22 either side.
    final whole = k <= 1 ? 220.0 : (k < 1.3 ? 210.0 : 200.0);
    final Widget art;
    if (item == MusicItem.coverFlow ||
        item == MusicItem.playlists ||
        item == MusicItem.search ||
        covers.isEmpty) {
      art = ArtFrame(
        size: whole,
        use: ArtUse.home,
        art: item == MusicItem.coverFlow && covers.isNotEmpty
            ? CoverArt(covers.first.art, decodeWidth: whole)
            : null,
      );
    } else {
      final small = (whole - 8) / 2;
      Widget one(int i) => ArtFrame(
        size: small,
        use: ArtUse.home,
        art: CoverArt(covers[i % covers.length].art, decodeWidth: small),
      );
      art = SizedBox(
        width: whole,
        height: whole,
        child: Column(
          children: [
            Row(children: [one(0), const SizedBox(width: 8), one(1)]),
            const SizedBox(height: 8),
            Row(children: [one(2), const SizedBox(width: 8), one(3)]),
          ],
        ),
      );
    }

    return PreviewPane(
      child: ExcludeSemantics(
        child: Column(
          children: [
            art,
            const SizedBox(height: 14),
            NeupodText(
              item.label,
              type.body(theme.style.tokens.ink),
              align: TextAlign.center,
            ),
            NeupodText(
              item.caption,
              type.small(theme.style.list.secondary),
              align: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}

List<Album> _firstOfEachArtist(List<Album> byArtist) {
  final seen = <String?>{};
  return [
    for (final a in byArtist)
      if (a.artistId != null && seen.add(a.artistId)) a,
  ];
}

/// The albums whose covers preview [item].
final _coversProvider = FutureProvider.family<List<Album>, MusicItem>((
  ref,
  item,
) async {
  final library = await ref.watch(libraryViewProvider.future);
  return switch (item) {
    // Cover Flow is the Albums list's second view (owner decision 14).
    MusicItem.coverFlow => await library.albums(),
    // The first album of each artist, in artist order: one read of the
    // album table, not an albums call per artist.
    MusicItem.artists => _firstOfEachArtist(await library.albumsByArtist()),
    MusicItem.albums => await library.albums(),
    MusicItem.songs => (await library.albums()).reversed.toList(),
    MusicItem.genres => [
      for (final g in await library.genres())
        ...(await library.albumsOfGenre(g.id)).take(1),
    ],
    MusicItem.compilations => await library.compilations(),
    _ => const <Album>[],
  };
});
