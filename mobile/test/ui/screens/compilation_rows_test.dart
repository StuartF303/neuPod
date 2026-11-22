// SPDX-License-Identifier: MPL-2.0

// A compilation's rows show no artist; only its album header says "Various
// Artists". The firmware: `lib_list_row` (lib_reader.c) gives an album row
// its artist only when the album has one (`en.artist != LIB_NONE`), so the
// Albums and Compilations lists (`lvw_row`, lib_view.c) and the Cover Flow
// caption (`flow_texts`, ui_library.c) have none for a compilation, while
// `album_header` (lib_view.c) writes "Various Artists".

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/albums_screen.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/browse/collection_screen.dart';
import 'package:neupod/ui/screens/browse/compilations_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';

import 'screens_harness.dart';

const _comp = Album(
  id: 'album:comp|hits',
  title: 'Hits',
  artist: 'Various Artists',
  songCount: 2,
  year: 1999,
  compilation: true,
  art: ArtRef(seed: 1),
  sortLetter: 'H',
);

const _abba = Album(
  id: 'album:abba|gold',
  title: 'Gold',
  artist: 'Abba',
  artistId: 'artist:abba',
  songCount: 1,
  year: 1992,
  art: ArtRef(seed: 2),
  sortLetter: 'G',
);

const _song = Song(
  id: '1',
  title: 'Track',
  artist: 'Someone',
  album: 'Hits',
  albumId: 'album:comp|hits',
  uri: 'file:///x.flac',
  sortLetter: 'T',
);

void main() {
  test('a compilation row has no artist; any other keeps its own', () {
    expect(albumRowArtist(_comp), '');
    expect(albumArtistYear(_comp), '1999');
    expect(albumRowArtist(_abba), 'Abba');
    expect(albumArtistYear(_abba), 'Abba · 1992');
    const noYear = Album(
      id: 'c',
      title: 'Mix',
      artist: 'Various Artists',
      songCount: 1,
      compilation: true,
      art: ArtRef(seed: 3),
    );
    expect(albumArtistYear(noYear), '');
  });

  for (final (name, screen) in <(String, Widget)>[
    ('Albums', const AlbumsScreen()),
    ('Compilations', const CompilationsScreen()),
    (
      'a genre',
      CollectionScreen.genre(
        const Genre(id: 'g', name: 'Pop', albumCount: 2, songCount: 3),
      ),
    ),
    ('Cover Flow', const CoverFlowScreen()),
  ]) {
    testWidgets('$name: a compilation shows no artist', (tester) async {
      await pumpShell(tester, home: (_) => screen, library: _Library());
      await tester.pumpAndSettle();
      expect(find.text('Hits'), findsWidgets);
      expect(find.textContaining('Various Artists'), findsNothing);
    });
  }

  testWidgets('the Albums list keeps the artist of an ordinary album', (
    tester,
  ) async {
    await pumpShell(
      tester,
      home: (_) => const AlbumsScreen(),
      library: _Library(),
    );
    await tester.pumpAndSettle();
    expect(find.text('Abba · 1992'), findsOneWidget);
    expect(find.text('1999'), findsOneWidget);
  });

  testWidgets('the album header says Various Artists', (tester) async {
    await pumpShell(
      tester,
      home: (_) => AlbumScreen(albumId: _comp.id),
      library: _Library(),
    );
    await tester.pumpAndSettle();
    expect(find.text('Various Artists'), findsOneWidget);
  });
}

/// A library of one compilation (first, as Cover Flow shows it) and one
/// ordinary album.
class _Library implements LibraryView {
  @override
  Future<List<Album>> albums() async => [_abba, _comp];

  @override
  Future<List<Album>> albumsForCoverFlow() async => [_comp, _abba];

  @override
  Future<List<Album>> compilations() async => [_comp];

  @override
  Future<List<Album>> albumsOfGenre(String genreId) async => [_comp, _abba];

  @override
  Future<AlbumDetail?> album(String albumId) async => albumId == _comp.id
      ? const AlbumDetail(
          album: _comp,
          discs: [
            Disc(number: 1, songs: [_song]),
          ],
        )
      : null;

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}
