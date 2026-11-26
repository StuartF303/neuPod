// SPDX-License-Identifier: MPL-2.0

// Settings > Library, driven with the wheel: the folders, Add Folder...,
// Update Library, Remove, the shared music collection on Android (turn on,
// a refused permission, turn off), the scan's progress row and its cancel,
// and the empty library pointing to it from Home and Songs.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_row.dart';
import 'package:neupod/ui/screens/browse/home_screen.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod/ui/screens/browse/songs_screen.dart';
import 'package:neupod/ui/screens/panel/panel_overlay.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod_storage/neupod_storage.dart' show sharedCollectionRootId;

import '../../fakes/fake_folder_access.dart';
import '../../fakes/fake_library_scanner.dart';
import 'screens_harness.dart';

const documents = LibraryFolder(
  id: 'documents',
  name: 'neuPod',
  kind: FolderKind.appDocuments,
  available: true,
);
const music = LibraryFolder(
  id: 'content://tree/Music',
  name: 'Music',
  kind: FolderKind.pickedFolder,
  available: true,
);
const drive = LibraryFolder(
  id: 'bookmark-1',
  name: 'USB',
  kind: FolderKind.pickedFolder,
  available: false,
);

const collection = LibraryFolder(
  id: sharedCollectionRootId,
  name: 'Music on This Phone',
  kind: FolderKind.sharedCollection,
  available: true,
);

/// Songs in `Music` (2), `USB` (1) and the shared collection (1), as the
/// repository reports them.
class _Songs implements LibraryRepository {
  @override
  Stream<List<Track>> watchSongs() => Stream.value([
    for (final (i, id) in [
      (1, music.id),
      (2, music.id),
      (3, drive.id),
      (4, collection.id),
    ])
      Track(
        id: i,
        source: TrackSource(
          id == collection.id ? SourceKind.mediaStore : SourceKind.localFolder,
          id,
        ),
        location: TrackLocation(relPath: '$i.flac', uri: 'file:///$i.flac'),
        codec: 'flac',
      ),
  ]);
}

Finder _row(String text) =>
    find.descendant(of: find.byType(NeupodRow), matching: find.text(text));

String _wheelValue(ShellRig rig) => rig.bus.semantics.value.value;

Future<void> _select(WidgetTester tester, ShellRig rig) async {
  rig.press(WheelButton.centre);
  await settle(tester);
}

Future<void> _step(WidgetTester tester, ShellRig rig, int n) async {
  rig.step(n);
  await settle(tester);
}

/// Home, then Settings, then Library, all with the wheel.
Future<ShellRig> _openLibrary(
  WidgetTester tester,
  FakeFolderAccess access,
  FakeLibraryScanner scanner,
) async {
  final rig = await pumpShell(
    tester,
    home: (_) => const HomeScreen(),
    overrides: [
      folderAccessProvider.overrideWithValue(access),
      libraryScannerProvider.overrideWithValue(scanner),
      libraryRepositoryProvider.overrideWithValue(_Songs()),
    ],
  );
  await _step(tester, rig, 2); // Settings
  await _select(tester, rig);
  expect(_row('Library'), findsOneWidget);
  await _step(tester, rig, 2); // Style, Mode, Library
  await _select(tester, rig);
  expect(rig.container.read(shellProvider).top!.name, 'Library');
  return rig;
}

void main() {
  testWidgets('Settings > Library with the wheel lists the folders', (
    tester,
  ) async {
    final rig = await _openLibrary(
      tester,
      FakeFolderAccess([documents, music, drive]),
      FakeLibraryScanner(),
    );
    expect(_row('neuPod'), findsOneWidget);
    expect(_row('On My iPhone'), findsOneWidget);
    expect(_row('Music'), findsOneWidget);
    expect(_row('Not available'), findsOneWidget);
    expect(_row('Add Folder…'), findsOneWidget);
    expect(_row('Update Library'), findsOneWidget);
    expect(_wheelValue(rig), 'neuPod, On My iPhone, 1 of 5');
  });

  testWidgets('the Documents folder explains itself and cannot be removed', (
    tester,
  ) async {
    final rig = await _openLibrary(
      tester,
      FakeFolderAccess([documents]),
      FakeLibraryScanner(),
    );
    await _select(tester, rig);
    expect(rig.container.read(shellProvider).panel?.title, 'On My iPhone');
    expect(find.byType(PanelOverlay), findsOneWidget);
    expect(rig.container.read(shellProvider).top!.name, 'Library');
  });

  testWidgets('Add Folder adds the picked folder and starts a scan', (
    tester,
  ) async {
    final access = FakeFolderAccess()..nextPick = music;
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, access, scanner);
    expect(_wheelValue(rig), startsWith('Add Folder'));
    await _select(tester, rig);
    expect(_row('Music'), findsOneWidget);
    expect(scanner.started, 1);
    expect(_row('Building library'), findsOneWidget);
  });

  testWidgets('a cancelled pick adds nothing and scans nothing', (
    tester,
  ) async {
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, FakeFolderAccess(), scanner);
    await _select(tester, rig);
    expect(scanner.started, 0);
    expect(rig.container.read(shellProvider).panel, isNull);
  });

  testWidgets('a refused folder says why in a panel', (tester) async {
    final access = FakeFolderAccess()
      ..pickError = const FolderAccessException('busy');
    final rig = await _openLibrary(tester, access, FakeLibraryScanner());
    await _select(tester, rig);
    final panel = rig.container.read(shellProvider).panel!;
    expect(panel.title, "Can't add folder");
    expect(panel.message, 'The folder picker is already open.');
  });

  testWidgets('Update Library starts a scan', (tester) async {
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, FakeFolderAccess(), scanner);
    await _step(tester, rig, 1);
    expect(_wheelValue(rig), startsWith('Update Library'));
    await _select(tester, rig);
    expect(scanner.started, 1);
    expect(
      rig.container.read(libraryScanProvider.notifier).startedByUser,
      isTrue,
    );

    // It ends as the firmware's Update Library does: the counts, then Done.
    await scanner.finish(found: 3);
    await settle(tester);
    final panel = rig.container.read(shellProvider).panel!;
    expect(panel.title, 'Library updated');
    expect(panel.message, await libraryUpdatedLine(gateLibrary));
    expect(
      panel.message,
      matches(RegExp(r'^\d[\d,]* songs · \d+ artists · \d+ albums$')),
    );
    expect(panel.okLabel, 'Done');
  });

  testWidgets('a failed Update Library leaves the panel to the app', (
    tester,
  ) async {
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, FakeFolderAccess(), scanner);
    await _step(tester, rig, 1);
    await _select(tester, rig);
    scanner.current.addError(StateError('x'));
    await settle(tester);
    // No Library updated (NeuPodApp shows the failure; see neupod_app_test).
    expect(rig.container.read(shellProvider).panel, isNull);
    expect(_row('Update Library'), findsOneWidget);
  });

  test('the Library updated line is singular at one', () async {
    expect(await libraryUpdatedLine(_One()), '1 song · 1 artist · 1 album');
  });

  testWidgets('Remove asks first, as the firmware does; Select confirms, '
      'then it gives up the folder, goes back and rescans', (tester) async {
    final access = FakeFolderAccess([music, drive]);
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, access, scanner);
    await _select(tester, rig); // Music
    expect(rig.container.read(shellProvider).top!.name, 'Music');
    expect(_row('Remove'), findsOneWidget);
    await _select(tester, rig); // Remove: the question, nothing done yet
    expect(rig.container.read(shellProvider).top!.name, 'Remove');
    expect(find.text('Remove Music?'), findsOneWidget);
    expect(find.text('2 songs come off the list.'), findsOneWidget);
    expect(find.text('Select = Remove  ·  Menu = Keep'), findsOneWidget);
    expect(_wheelValue(rig), contains('Remove Music?'));
    expect(access.removed, isEmpty);
    expect(scanner.started, 0);
    await _step(tester, rig, 3); // a turn does nothing
    expect(access.removed, isEmpty);
    expect(rig.container.read(shellProvider).top!.name, 'Remove');
    await _select(tester, rig); // Select = Remove
    expect(access.removed, ['content://tree/Music']);
    expect(rig.container.read(shellProvider).top!.name, 'Library');
    expect(_row('Music'), findsNothing);
    expect(_row('USB'), findsOneWidget);
    expect(scanner.started, 1);
  });

  testWidgets('Menu on the question keeps the folder and its place', (
    tester,
  ) async {
    final access = FakeFolderAccess([music, drive]);
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, access, scanner);
    await _select(tester, rig); // Music
    await _select(tester, rig); // Remove: the question
    expect(find.text('Remove Music?'), findsOneWidget);
    rig.press(WheelButton.menu); // Menu = Keep
    await settle(tester);
    // Back on the folder's own screen, nothing given up, nothing scanned.
    expect(rig.container.read(shellProvider).top!.name, 'Music');
    expect(_row('Remove'), findsOneWidget);
    expect(access.removed, isEmpty);
    expect(scanner.started, 0);
    rig.press(WheelButton.menu);
    await settle(tester);
    expect(rig.container.read(shellProvider).top!.name, 'Library');
    expect(_row('Music'), findsOneWidget);
    expect(access.removed, isEmpty);
  });

  test('the Remove sub-line counts as the firmware does', () {
    expect(removeFolderSub(0), '0 songs come off the list.');
    expect(removeFolderSub(1), '1 song come off the list.');
    expect(removeFolderSub(12), '12 songs come off the list.');
  });

  testWidgets('the question counts only that folder\'s songs', (tester) async {
    final rig = await _openLibrary(
      tester,
      FakeFolderAccess([music, drive]),
      FakeLibraryScanner(),
    );
    await _step(tester, rig, 1); // USB
    await _select(tester, rig);
    await _select(tester, rig); // Remove
    expect(find.text('Remove USB?'), findsOneWidget);
    expect(find.text('1 song come off the list.'), findsOneWidget);
  });

  testWidgets('Menu on a folder keeps it', (tester) async {
    final access = FakeFolderAccess([music]);
    final rig = await _openLibrary(tester, access, FakeLibraryScanner());
    await _select(tester, rig);
    rig.press(WheelButton.menu);
    await settle(tester);
    expect(access.removed, isEmpty);
    expect(_row('Music'), findsOneWidget);
  });

  group('the shared music collection (Android)', () {
    testWidgets('not offered (iOS): no row for it', (tester) async {
      await _openLibrary(
        tester,
        FakeFolderAccess([music]),
        FakeLibraryScanner(),
      );
      expect(_row(addSharedCollectionTitle), findsNothing);
    });

    testWidgets('offered and off: a row after Add Folder turns it on, asking '
        'for the permission only then, and starts a scan', (tester) async {
      final access = FakeFolderAccess([music])
        ..offersSharedCollection = true
        ..sharedCollection = collection;
      final scanner = FakeLibraryScanner();
      final rig = await _openLibrary(tester, access, scanner);
      expect(access.sharedCollectionAsks, 0);
      expect(_row('Add Music on This Phone'), findsOneWidget);
      await _step(
        tester,
        rig,
        2,
      ); // Music, Add Folder…, Add Music on This Phone
      expect(_wheelValue(rig), startsWith('Add Music on This Phone'));
      await _select(tester, rig);
      expect(access.sharedCollectionAsks, 1);
      expect(scanner.started, 1);
      // Listed as a folder is; the add row is gone.
      expect(_row('Music on This Phone'), findsOneWidget);
      expect(_row('Add Music on This Phone'), findsNothing);
      expect(rig.container.read(shellProvider).panel, isNull);
    });

    testWidgets('a refused permission says how to allow it, and scans '
        'nothing', (tester) async {
      final access = FakeFolderAccess()..offersSharedCollection = true;
      final scanner = FakeLibraryScanner();
      final rig = await _openLibrary(tester, access, scanner);
      await _step(tester, rig, 1); // Add Folder…, Add Music on This Phone
      await _select(tester, rig);
      final panel = rig.container.read(shellProvider).panel!;
      expect(panel.title, "Can't read music");
      expect(panel.message, sharedCollectionRefused);
      expect(scanner.started, 0);
      expect(_row('Add Music on This Phone'), findsOneWidget);
    });

    testWidgets('on: Turn Off asks first, as Remove does, then turns it off '
        'and rescans', (tester) async {
      final access = FakeFolderAccess([music, collection])
        ..offersSharedCollection = true;
      final scanner = FakeLibraryScanner();
      final rig = await _openLibrary(tester, access, scanner);
      expect(_row('Add Music on This Phone'), findsNothing);
      await _step(tester, rig, 1); // Music on This Phone
      await _select(tester, rig);
      expect(
        rig.container.read(shellProvider).top!.name,
        'Music on This Phone',
      );
      expect(_row('Turn Off'), findsOneWidget);
      await _select(tester, rig);
      expect(find.text('Turn off Music on This Phone?'), findsOneWidget);
      expect(find.text('1 song come off the list.'), findsOneWidget);
      expect(find.text('Select = Turn Off  ·  Menu = Keep'), findsOneWidget);
      expect(access.removed, isEmpty);
      await _select(tester, rig);
      expect(access.removed, [sharedCollectionRootId]);
      expect(rig.container.read(shellProvider).top!.name, 'Library');
      expect(_row('Music on This Phone'), findsNothing);
      expect(_row('Add Music on This Phone'), findsOneWidget);
      expect(scanner.started, 1);
    });

    testWidgets('on but its permission taken away: Not available', (
      tester,
    ) async {
      const gone = LibraryFolder(
        id: sharedCollectionRootId,
        name: 'Music on This Phone',
        kind: FolderKind.sharedCollection,
        available: false,
      );
      await _openLibrary(
        tester,
        FakeFolderAccess([gone])..offersSharedCollection = true,
        FakeLibraryScanner(),
      );
      expect(_row('Music on This Phone'), findsOneWidget);
      expect(_row('Not available'), findsOneWidget);
    });
  });

  testWidgets('a running scan shows its progress; Select cancels it', (
    tester,
  ) async {
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, FakeFolderAccess(), scanner);
    await rig.container.read(libraryScanProvider.notifier).start();
    scanner.say(const ScanProgress(filesFound: 1204, filesRead: 37));
    await settle(tester);
    expect(_row('Building library'), findsOneWidget);
    expect(_row('1,204 found · 37 read · Select: cancel'), findsOneWidget);
    expect(_row('Update Library'), findsNothing);

    await _step(tester, rig, 1);
    await _select(tester, rig);
    expect(scanner.cancelled, 1);
    expect(rig.container.read(libraryScanProvider)!.error, scanCancelled);
    // As on the firmware, a cancel says nothing; Update Library is back.
    expect(rig.container.read(shellProvider).panel, isNull);
    expect(_row('Update Library'), findsOneWidget);
  });

  testWidgets('a scan that finishes puts Update Library back, no panel', (
    tester,
  ) async {
    final scanner = FakeLibraryScanner();
    final rig = await _openLibrary(tester, FakeFolderAccess(), scanner);
    await rig.container.read(libraryScanProvider.notifier).start();
    await settle(tester);
    await scanner.finish(found: 3);
    await settle(tester);
    expect(_row('Update Library'), findsOneWidget);
    expect(rig.container.read(shellProvider).panel, isNull);
  });

  group('an empty library points to Settings > Library', () {
    testWidgets('on Home', (tester) async {
      await pumpShell(
        tester,
        home: (_) => const HomeScreen(),
        library: _Empty(),
      );
      expect(find.text('Nothing playing'), findsOneWidget);
      expect(find.text(noSongsTitle), findsOneWidget);
      expect(find.text(addMusicHint), findsOneWidget);
    });

    testWidgets('a library with songs shows the count, no hint', (
      tester,
    ) async {
      await pumpShell(tester, home: (_) => const HomeScreen());
      expect(find.text(addMusicHint), findsNothing);
      expect(find.text(noSongsTitle), findsNothing);
    });

    testWidgets('in Songs, whose row opens Library', (tester) async {
      final rig = await pumpShell(
        tester,
        home: (_) => const SongsScreen(),
        library: _Empty(),
        overrides: [
          folderAccessProvider.overrideWithValue(FakeFolderAccess()),
          libraryScannerProvider.overrideWithValue(FakeLibraryScanner()),
        ],
      );
      expect(_row(noSongsTitle), findsOneWidget);
      expect(_row(addMusicHint), findsOneWidget);
      expect(_row('None yet'), findsNothing);
      await _select(tester, rig);
      expect(rig.container.read(shellProvider).top!.name, 'Library');
      expect(find.byType(LibraryScreen), findsOneWidget);
    });
  });
}

/// A library of one song by one artist on one album.
class _One implements LibraryView {
  @override
  Future<List<Song>> songs() async => [(await gateLibrary.songs()).first];

  @override
  Future<List<Artist>> artists() async => [(await gateLibrary.artists()).first];

  @override
  Future<List<Album>> albums() async => [(await gateLibrary.albums()).first];

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}

/// A library with nothing in it.
class _Empty implements LibraryView {
  @override
  Future<List<Song>> songs() async => const [];

  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName}');
}
