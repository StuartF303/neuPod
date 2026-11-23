// SPDX-License-Identifier: MPL-2.0

// Disc headings on the album screen. The firmware draws none; its album
// order is disc, then track (`alb_cmp`, lib_build.c), so songs with no disc
// number come first. Lead decision (2026-11-23, `Disc` in library_view.dart):
// those songs keep their place under no heading, each disc number after them
// gets "Disc n", and an album of one disc has no heading at all.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';

import 'screens_harness.dart';

const _album = Album(
  id: 'album:band|double',
  title: 'Double',
  artist: 'Band',
  artistId: 'artist:band',
  songCount: 3,
  art: ArtRef(seed: 1),
  sortLetter: 'D',
);

Song _song(String id, String title, int? disc) => Song(
  id: id,
  title: title,
  artist: 'Band',
  album: 'Double',
  albumId: _album.id,
  uri: 'file:///$id.flac',
  disc: disc,
  sortLetter: 'A',
);

void main() {
  testWidgets('songs without a disc number come first, under no heading', (
    tester,
  ) async {
    await pumpShell(
      tester,
      home: (_) => AlbumScreen(albumId: _album.id),
      library: _Library(
        AlbumDetail(
          album: _album,
          discs: [
            Disc(number: null, songs: [_song('1', 'Bonus', null)]),
            Disc(number: 1, songs: [_song('2', 'A1', 1)]),
            Disc(number: 2, songs: [_song('3', 'B1', 2)]),
          ],
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.text('Bonus'), findsOneWidget);
    expect(find.text('Disc 0'), findsNothing);
    expect(find.text('Disc 1'), findsOneWidget);
    expect(find.text('Disc 2'), findsOneWidget);
    // In album order: the song with no disc number above Disc 1.
    expect(
      tester.getTopLeft(find.text('Bonus')).dy,
      lessThan(tester.getTopLeft(find.text('Disc 1')).dy),
    );
  });

  testWidgets('an album of one disc has no heading', (tester) async {
    await pumpShell(
      tester,
      home: (_) => AlbumScreen(albumId: _album.id),
      library: _Library(
        AlbumDetail(
          album: _album,
          discs: [
            Disc(number: 1, songs: [_song('1', 'Only', null)]),
          ],
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.text('Only'), findsOneWidget);
    expect(find.textContaining('Disc'), findsNothing);
  });
}

class _Library implements LibraryView {
  _Library(this.detail);

  final AlbumDetail detail;

  @override
  Future<AlbumDetail?> album(String albumId) async =>
      albumId == _album.id ? detail : null;

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}
