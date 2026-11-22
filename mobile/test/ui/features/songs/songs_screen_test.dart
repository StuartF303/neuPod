// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/ui/features/songs/songs_screen.dart';

class FakeRepository implements LibraryRepository {
  FakeRepository(this.songs);
  final List<Track> songs;

  @override
  Stream<List<Track>> watchSongs() => Stream.value(songs);
}

class FakeScanner implements LibraryScanner {
  // ignore: close_sinks, closed by the test that finishes a scan
  final controller = StreamController<ScanProgress>();
  int started = 0;

  @override
  Stream<ScanProgress> scan() {
    started++;
    return controller.stream;
  }
}

Track track(
  int id,
  String rel, {
  String title = '',
  String artist = '',
  String album = '',
  String? reason,
}) => Track(
  id: id,
  source: const TrackSource(SourceKind.localFolder, 'f'),
  location: TrackLocation(relPath: rel, uri: 'file:///$rel'),
  codec: 'flac',
  title: title,
  artist: artist,
  album: album,
  unreadableReason: reason,
);

Future<FakeScanner> pump(WidgetTester tester, List<Track> songs) async {
  final scanner = FakeScanner();
  await tester.pumpWidget(
    ProviderScope(
      retry: (_, _) => null,
      overrides: [
        libraryRepositoryProvider.overrideWithValue(FakeRepository(songs)),
        libraryScannerProvider.overrideWithValue(scanner),
      ],
      child: const MaterialApp(home: SongsScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return scanner;
}

void main() {
  testWidgets('rows show the title over "artist · album", in the given order', (
    tester,
  ) async {
    await pump(tester, [
      track(
        1,
        'a.flac',
        title: 'Hey Jude',
        artist: 'The Beatles',
        album: 'Past Masters',
      ),
      track(2, 'Folder/Untitled Song.mp3', artist: 'Someone'),
      track(3, 'x.flac', title: 'Bare'),
    ]);
    expect(find.text('Hey Jude'), findsOneWidget);
    expect(find.text('The Beatles · Past Masters'), findsOneWidget);
    expect(
      find.text('Untitled Song'),
      findsOneWidget,
    ); // file name when untagged
    expect(find.text('Someone'), findsOneWidget);
    final titles = tester
        .widgetList<ListTile>(find.byType(ListTile))
        .map((t) => ((t.title! as Text).data))
        .toList();
    expect(titles, ['Hey Jude', 'Untitled Song', 'Bare']);
  });

  testWidgets('an unreadable file is greyed with its reason', (tester) async {
    await pump(tester, [track(1, 'bad.flac', reason: 'not a FLAC file')]);
    expect(find.text('not a FLAC file'), findsOneWidget);
    expect(find.byType(Opacity), findsOneWidget);
  });

  testWidgets('empty library', (tester) async {
    await pump(tester, []);
    expect(find.text('No songs. Add a folder, then Scan.'), findsOneWidget);
  });

  testWidgets('Scan starts the scanner and shows progress', (tester) async {
    final scanner = await pump(tester, []);
    await tester.tap(find.text('Scan'));
    await tester.pump();
    expect(scanner.started, 1);

    scanner.controller.add(const ScanProgress(filesFound: 12, filesRead: 3));
    await tester.pump();
    expect(find.text('Scanning: 12 files found, 3 read'), findsOneWidget);

    // A second tap while scanning does nothing.
    await tester.tap(find.text('Scan'));
    await tester.pump();
    expect(scanner.started, 1);

    scanner.controller.add(
      const ScanProgress(filesFound: 12, filesRead: 12, done: true),
    );
    await scanner.controller.close();
    await tester.pumpAndSettle();
    expect(find.text('Scan finished: 12 files'), findsOneWidget);
  });
}
