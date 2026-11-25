// SPDX-License-Identifier: MPL-2.0

// Album art on a device: a folder with a FLAC and a Folder.jpg is scanned,
// the library view gives the album its picture, the art cache decodes it in a
// background isolate into the app's own directory, and the cover shows it.
// A second album's FLAC has its own front-cover PICTURE block as well as a
// Folder.jpg (as the owner's albums do): the embedded picture wins, as on the
// device.
//
// The folder is served through a stand-in for the plugin's walk (the real
// picker cannot be driven from here; folder_access_test.dart covers it), with
// `file://` entries, so the files are read through the plugin's own
// FileReaderHandle, in a real isolate, on the device.

import 'dart:io';

import 'package:drift/native.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:integration_test/integration_test.dart';
import 'package:neupod/app/art_providers.dart';
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod_storage/neupod_storage.dart';
// ignore: implementation_imports
import 'package:neupod_storage/src/messages.g.dart';

/// One folder on the device's disk as the only root.
class _DirApi extends NeupodStorageApi {
  _DirApi(this.dir);

  final Directory dir;

  @override
  Future<String> startWalk(String rootId) async => 'w';

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async {
    final entries = [
      for (final f in dir.listSync(recursive: true).whereType<File>())
        StorageEntry(
          relPath: f.path.substring(dir.path.length + 1),
          uri: f.uri.toString(),
          size: f.lengthSync(),
          modifiedMs: f.lastModifiedSync().millisecondsSinceEpoch,
          state: EntryState.local,
        ),
    ];
    return WalkBatch(entries: entries, done: true, complete: true);
  }
}

class _OneFolder implements FolderAccess {
  @override
  Future<List<LibraryFolder>> folders() async => const [
    LibraryFolder(
      id: 'it',
      name: 'Music',
      kind: FolderKind.pickedFolder,
      available: true,
    ),
  ];

  @override
  Future<LibraryFolder?> pickFolder() async => null;

  @override
  Future<void> removeFolder(String id) async {}

  @override
  bool get offersSharedCollection => false;

  @override
  Future<LibraryFolder?> addSharedCollection() async => null;
}

Future<List<int>> _asset(String name) async {
  final d = await rootBundle.load('integration_test/assets/test_media/$name');
  return d.buffer.asUint8List(d.offsetInBytes, d.lengthInBytes);
}

/// The colour at the top right of a cached picture: the fixtures are two
/// colours split on the diagonal (Folder.jpg blue there, the embedded
/// picture red).
(int, int, int) _topRight(String path) {
  final i = img.decodeJpg(File(path).readAsBytesSync())!;
  final p = i.getPixel(i.width - 4, 3);
  return (p.r.toInt(), p.g.toInt(), p.b.toInt());
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // As in app_launch_test.dart: under `xcodebuild test` iOS turns
  // accessibility on just after launch, and the SemanticsHandle the binding
  // then opens must not land inside the test. Wait for it first (bounded).
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

  testWidgets('a scanned album with Folder.jpg shows its art', (tester) async {
    final root = Directory('${Directory.systemTemp.path}/neupod_art_it');
    if (root.existsSync()) root.deleteSync(recursive: true);
    final music = Directory('${root.path}/music');
    final db = LibraryDatabase(NativeDatabase.memory());
    final view = DriftLibraryView(db);
    addTearDown(() async {
      await view.dispose();
      await db.close();
      root.deleteSync(recursive: true);
    });

    late List<Album> albums;
    late ArtCache cache;
    late String folderArt, embeddedArt;
    await tester.runAsync(() async {
      // The test writes the folder; the app only reads it.
      Future<void> put(String rel, String asset) async => (File(
        '${music.path}/$rel',
      )..createSync(recursive: true)).writeAsBytesSync(await _asset(asset));
      await put('Band/2001 Plain/01 One.flac', 'flac16_44k_st.flac');
      await put('Band/2001 Plain/Folder.jpg', 'Folder.jpg');
      await put('Band/2002 Pictured/01 Two.flac', 'front_jpeg.flac');
      await put('Band/2002 Pictured/Folder.jpg', 'Folder.jpg');

      final storage = NeupodStorage(api: _DirApi(music));
      final last = await LocalLibraryScanner(
        folders: _OneFolder(),
        storage: storage,
        db: db,
      ).scan().last;
      expect(last.done, isTrue);
      expect(last.filesFound, 2);

      albums = await view.albums();
      cache = ArtCache(
        directory: () async => '${root.path}/art',
        open: storageArtOpener(storage),
      );
      final byFolder = {
        for (final a in albums) (await view.album(a.id))!.songs.first.uri: a,
      };
      final plain = byFolder.entries
          .firstWhere((e) => e.key.contains('Plain'))
          .value;
      final pictured = byFolder.entries
          .firstWhere((e) => e.key.contains('Pictured'))
          .value;
      folderArt = (await cache.file(plain.art, ArtSize.full))!;
      embeddedArt = (await cache.file(pictured.art, ArtSize.full))!;
    });

    final (fr, _, fb) = _topRight(folderArt);
    expect(fb > 120 && fr < 100, isTrue, reason: 'Folder.jpg (blue)');
    final (er, _, eb) = _topRight(embeddedArt);
    expect(er > 150 && eb < 100, isTrue, reason: 'the embedded picture (red)');
    expect(folderArt, startsWith('${root.path}/art/'));

    await tester.pumpWidget(
      ProviderScope(
        overrides: [artCacheProvider.overrideWithValue(cache)],
        child: Directionality(
          textDirection: TextDirection.ltr,
          child: Row(
            children: [
              for (final a in albums)
                // Small enough for two on the narrowest phone; decoded as
                // Now Playing draws it (the full-size file).
                SizedBox.square(
                  dimension: 120,
                  child: CoverArt(a.art, decodeWidth: 220),
                ),
            ],
          ),
        ),
      ),
    );
    // The cache's answers arrive on later frames.
    for (var i = 0; i < 20; i++) {
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 50)),
      );
      await tester.pump();
    }
    final shown = {
      for (final w in tester.widgetList<Image>(find.byType(Image)))
        if (w.image case ResizeImage(imageProvider: FileImage(:final file)))
          file.path
        else if (w.image case FileImage(:final file))
          file.path,
    };
    expect(shown, {folderArt, embeddedArt});
  });
}
