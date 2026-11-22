// SPDX-License-Identifier: MPL-2.0

// Cover Flow is the Albums list's second view, as on the device (owner
// decision 14). Expectations come from the firmware, not from the screen's
// code:
//
// - app/ui/ui_library.c (header, LB-6): the Albums list's Cover Flow shows
//   the list's albums at the list's selection, so in the Albums list's
//   order: `albname.rec` sorted by `key_cmp` (lib_build.c), the title key
//   and then the album's id (its place in the album table, by artist, with
//   compilations last). Equal titles of different artists stay apart and
//   come in artist order, a compilation after them; equal keys of one
//   artist fold into one album (`stage_entity`).
// - app/ui/ui_library.c `flow_texts`: the caption is the row's detail, then
//   the year when not 0, then the count when not 0 as `%u song` for 1 and
//   `%u songs` otherwise, joined by " · ", each part only when present.
// - app/library/lib_view.c (LVW_ALBUMS): the row's detail is
//   `lib_list_row`'s `sub` (lib_reader.c), the album artist, empty for a
//   compilation (`en.artist != LIB_NONE`).

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/screens/browse/album_screen.dart';
import 'package:neupod/ui/screens/browse/cover_flow_screen.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';

import '../../data/library_view/seed.dart';
import 'screens_harness.dart';

Album _album(
  String title, {
  String artist = 'Abba',
  int? year,
  int songs = 1,
  bool compilation = false,
}) => Album(
  id: 'album:$artist|$title',
  title: title,
  artist: compilation ? 'Various Artists' : artist,
  artistId: compilation ? null : 'artist:$artist',
  songCount: songs,
  year: year,
  compilation: compilation,
  art: const ArtRef(seed: 1),
);

String _wheelValue(ShellRig rig) => rig.bus.semantics.value.value;

/// The spoken label of every album, walking Cover Flow from the first with
/// the wheel.
Future<List<String>> _walk(WidgetTester tester, ShellRig rig, int n) async {
  final out = [_wheelValue(rig)];
  for (var i = 1; i < n; i++) {
    rig.step(1);
    await settle(tester);
    out.add(_wheelValue(rig));
  }
  return out;
}

void main() {
  group('the caption, as flow_texts builds it', () {
    test('artist, year and count', () {
      expect(
        coverFlowCaption(_album('Gold', year: 1992, songs: 3)),
        'Abba · 1992 · 3 songs',
      );
    });

    test('1 song, n songs, and no digit grouping (%u)', () {
      expect(coverFlowCaption(_album('A', year: 1992)), 'Abba · 1992 · 1 song');
      expect(coverFlowCaption(_album('A', songs: 2)), 'Abba · 2 songs');
      expect(coverFlowCaption(_album('A', songs: 1204)), 'Abba · 1204 songs');
    });

    test('no year: the year is left out (0 is none too)', () {
      expect(coverFlowCaption(_album('A', songs: 12)), 'Abba · 12 songs');
      expect(
        coverFlowCaption(_album('A', year: 0, songs: 12)),
        'Abba · 12 songs',
      );
    });

    test('a compilation has no artist', () {
      expect(
        coverFlowCaption(
          _album('Hits', year: 1999, songs: 2, compilation: true),
        ),
        '1999 · 2 songs',
      );
      expect(
        coverFlowCaption(_album('Hits', songs: 1, compilation: true)),
        '1 song',
      );
    });

    test('no songs: no count; nothing at all is an empty caption', () {
      expect(
        coverFlowCaption(_album('A', year: 2001, songs: 0)),
        'Abba · 2001',
      );
      expect(coverFlowCaption(_album('Hits', songs: 0, compilation: true)), '');
    });
  });

  group('the spoken label', () {
    test('title, caption parts, place', () {
      expect(
        coverFlowLabel(_album('Gold', year: 1992), 1, 7),
        'Gold, Abba, 1992, 1 song, 2 of 7',
      );
      expect(
        coverFlowLabel(_album('Gold', songs: 10), 0, 7),
        'Gold, Abba, 10 songs, 1 of 7',
      );
    });

    test('a compilation says compilation where the artist would be', () {
      expect(
        coverFlowLabel(_album('Hits', songs: 0, compilation: true), 2, 7),
        'Hits, compilation, 3 of 7',
      );
      expect(
        coverFlowLabel(
          _album('Hits', year: 1999, songs: 2, compilation: true),
          2,
          7,
        ),
        'Hits, compilation, 1999, 2 songs, 3 of 7',
      );
    });
  });

  testWidgets('the order is the Albums list\'s: folded and tied titles, a '
      'compilation after its equals', (tester) async {
    // The scanned library: Cover Flow gets exactly what the Albums list
    // gets from the drift adapter.
    final albums = (await tester.runAsync(() async {
      final db = LibraryDatabase(NativeDatabase.memory());
      final view = DriftLibraryView(db);
      final src = await addSource(db);
      var n = 0;
      Future<void> add(String title, String artist, String album) async {
        n++;
        await addTrack(
          db,
          sourceId: src,
          relPath: 'Dir/f${'$n'.padLeft(4, '0')}.flac',
          title: title,
          artist: artist,
          album: album,
        );
      }

      await add('One', 'Zed', 'Gold');
      await add('Two', 'Zed', 'GOLD'); // folds into Zed's Gold
      await add('Three', 'Abba', 'gold'); // ties with Zed's: artist order
      await add('Four', 'Various Artists', 'Gold'); // a compilation: last
      await add('Five', 'Zed', 'Angstrom');
      await add('Six', 'Abba', 'Ångström'); // ties: Abba's first
      await add('Seven', 'Abba', 'Arrival');
      final out = await view.albums();
      await view.dispose();
      await db.close();
      return out;
    }))!;

    final rig = await pumpShell(
      tester,
      home: (_) => const CoverFlowScreen(),
      library: _Fixed(albums),
    );
    expect(await _walk(tester, rig, 6), [
      'Ångström, Abba, 1 song, 1 of 6',
      'Angstrom, Zed, 1 song, 2 of 6',
      'Arrival, Abba, 1 song, 3 of 6',
      'gold, Abba, 1 song, 4 of 6',
      'Gold, Zed, 2 songs, 5 of 6',
      'Gold, compilation, 1 song, 6 of 6',
    ]);
  });

  testWidgets('Cover Flow keeps the list\'s order: it does not sort again', (
    tester,
  ) async {
    // Deliberately in no sorted order: whatever albums() gives is shown.
    final given = [
      _album('Zebra', artist: 'Mia'),
      _album('Apple', artist: 'Zed'),
      _album('Mango', compilation: true),
    ];
    final rig = await pumpShell(
      tester,
      home: (_) => const CoverFlowScreen(),
      library: _Fixed(given),
    );
    expect(await _walk(tester, rig, 3), [
      'Zebra, Mia, 1 song, 1 of 3',
      'Apple, Zed, 1 song, 2 of 3',
      'Mango, compilation, 1 song, 3 of 3',
    ]);
  });

  final library = _Fixed([
    _album('Arrival', year: 1976, songs: 10),
    _album('Hits', year: 1999, songs: 2, compilation: true),
    _album('Single', songs: 1, artist: 'Zed'),
  ]);

  for (final family in StyleFamily.values) {
    testWidgets('${family.name}: the caption under the title, and the '
        'semantics label', (tester) async {
      final handle = tester.ensureSemantics();
      final rig = await pumpShell(
        tester,
        home: (_) => const CoverFlowScreen(),
        family: family,
        dark: family == StyleFamily.modern,
        library: library,
      );
      expect(find.text('Arrival'), findsWidgets);
      expect(find.text('Abba · 1976 · 10 songs'), findsOneWidget);
      // No separate artist line or Modern-only detail line any more.
      expect(find.text('Abba'), findsNothing);
      expect(find.text('1976 · 10 songs'), findsNothing);
      expect(
        find.bySemanticsLabel('Arrival, Abba, 1976, 10 songs, 1 of 3'),
        findsOneWidget,
      );

      rig.step(1);
      await settle(tester);
      expect(find.text('1999 · 2 songs'), findsOneWidget);
      expect(find.textContaining('Various Artists'), findsNothing);
      expect(
        find.bySemanticsLabel('Hits, compilation, 1999, 2 songs, 2 of 3'),
        findsOneWidget,
      );
      expect(_wheelValue(rig), 'Hits, compilation, 1999, 2 songs, 2 of 3');

      rig.step(1);
      await settle(tester);
      expect(find.text('Zed · 1 song'), findsOneWidget);
      handle.dispose();
    });
  }

  testWidgets('Select opens the centred album; Menu comes back to it', (
    tester,
  ) async {
    final rig = await pumpShell(
      tester,
      home: (_) => const CoverFlowScreen(),
      library: library,
    );
    rig.step(1);
    await settle(tester);
    rig.press(WheelButton.centre);
    await settle(tester);
    final pages = rig.container.read(shellProvider).pages;
    expect(pages, hasLength(2));
    expect(pages.last.name, 'Hits');
    expect(find.byType(AlbumScreen), findsOneWidget);

    rig.press(WheelButton.menu);
    await settle(tester);
    expect(rig.container.read(shellProvider).pages, hasLength(1));
    expect(_wheelValue(rig), 'Hits, compilation, 1999, 2 songs, 2 of 3');
    expect(find.text('1999 · 2 songs'), findsOneWidget);
  });
}

/// A library whose Albums list is [list]. Anything else Cover Flow asked
/// for (the album table, say) would throw.
class _Fixed implements LibraryView {
  _Fixed(this.list);

  final List<Album> list;

  @override
  Future<List<Album>> albums() async => list;

  @override
  Future<AlbumDetail?> album(String albumId) async {
    for (final a in list) {
      if (a.id == albumId) return AlbumDetail(album: a, discs: const []);
    }
    return null;
  }

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}
