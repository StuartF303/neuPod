// SPDX-License-Identifier: MPL-2.0

// Android's shared music collection (MediaStore) in the scanner, over a fake
// MediaStore: the collection's songs are read from their files, a rescan
// opens only what changed, a song inside a picked folder is listed once (by
// the folder, owner decision 21), turning the collection off removes its songs
// and keeps their ratings and play counts (decision 0011).

import 'dart:io';
import 'dart:typed_data';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/drift_track_stats_store.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/db/stats_database.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/data/scan/shared_collection.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/play_stats.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod_storage/neupod_storage.dart';
// ignore: implementation_imports
import 'package:neupod_storage/src/messages.g.dart';

import '../../fakes/fake_folder_access.dart';
import '../../fixtures/tagged_audio.dart';

/// The external storage provider's tree URI of [path] on the phone's own
/// storage, as the system folder picker returns it.
String treeOf(String path) =>
    'content://$externalStorageAuthority/tree/'
    '${Uri.encodeComponent('primary:$path')}';

/// A fake MediaStore over [volume], a host directory standing for the top of
/// the phone's storage. The collection lists the audio files under it, as
/// MediaStore does (`RELATIVE_PATH` + `DISPLAY_NAME`, a `content://media`
/// URI with a stable id, `DATE_MODIFIED` in whole seconds); a picked folder's
/// tree URI lists its folder as the folder walk does.
class FakeMediaStoreApi extends NeupodStorageApi {
  FakeMediaStoreApi(this.volume);

  final Directory volume;

  /// MediaStore's `_ID` of each relative path, kept across queries.
  final ids = <String, int>{};

  /// The file behind each collection URI.
  final items = <String, File>{};

  /// Relative paths MediaStore does not count as music (`IS_MUSIC` 0: a
  /// ringtone, a recording).
  final notMusic = <String>{};

  final _walks = <String, List<StorageEntry>>{};
  int _n = 0;

  @override
  Future<String> startWalk(String rootId) async {
    final entries = <StorageEntry>[];
    if (rootId == sharedCollectionRootId) {
      for (final f in volume.listSync(recursive: true).whereType<File>()) {
        final rel = f.path.substring(volume.path.length + 1);
        if (!audioExtensions.contains(extensionOf(rel))) continue;
        if (notMusic.contains(rel)) continue;
        final id = ids.putIfAbsent(rel, () => ids.length + 1);
        final uri = 'content://media/external_primary/audio/media/$id';
        items[uri] = f;
        final seconds = f.lastModifiedSync().millisecondsSinceEpoch ~/ 1000;
        entries.add(
          StorageEntry(
            relPath: rel,
            uri: uri,
            size: f.lengthSync(),
            modifiedMs: seconds * 1000,
            state: EntryState.local,
          ),
        );
      }
    } else {
      final loc = pickedFolderLocation(rootId)!;
      final dir = Directory('${volume.path}/${loc.path}');
      for (final f in dir.listSync(recursive: true).whereType<File>()) {
        entries.add(
          StorageEntry(
            relPath: f.path.substring(dir.path.length + 1),
            uri: f.uri.toString(),
            size: f.lengthSync(),
            modifiedMs: f.lastModifiedSync().millisecondsSinceEpoch,
            state: EntryState.local,
          ),
        );
      }
    }
    entries.sort((a, b) => a.relPath.compareTo(b.relPath));
    final id = 'w${_n++}';
    _walks[id] = entries;
    return id;
  }

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async =>
      WalkBatch(entries: _walks.remove(walkId)!, done: true, complete: true);

  @override
  Future<void> cancelWalk(String walkId) async => _walks.remove(walkId);
}

/// Opens collection items through their file (ContentResolver's
/// `openFileDescriptor` on the device), and counts the opens.
class FakeMediaStoreStorage extends NeupodStorage {
  FakeMediaStoreStorage(this.store) : super(api: store);

  final FakeMediaStoreApi store;
  final opened = <String>[];

  @override
  Future<ReaderHandle> openHandle(StorageEntry entry) async {
    opened.add(entry.relPath);
    final item = store.items[entry.uri];
    if (item != null) return FileReaderHandle(item.uri.toString());
    return super.openHandle(entry);
  }
}

const collection = LibraryFolder(
  id: sharedCollectionRootId,
  name: 'Music on This Phone',
  kind: FolderKind.sharedCollection,
  available: true,
);

LibraryFolder picked(String path) => LibraryFolder(
  id: treeOf(path),
  name: path.split('/').last,
  kind: FolderKind.pickedFolder,
  available: true,
);

void main() {
  group('where a picked folder is', () {
    test('the phone storage and an SD card, from the tree URI', () {
      expect(pickedFolderLocation(treeOf('Music/Library')), (
        volume: 'external_primary',
        path: 'Music/Library',
      ));
      expect(
        pickedFolderLocation(
          'content://$externalStorageAuthority/tree/1A2B-3C4D%3AMusic%2F',
        ),
        (volume: '1a2b-3c4d', path: 'Music'),
      );
      expect(pickedFolderLocation(treeOf('')), (
        volume: 'external_primary',
        path: '',
      ));
    });

    test('other providers and other URIs have no location', () {
      expect(
        pickedFolderLocation(
          'content://com.android.providers.downloads.documents/tree/raw%3A%2Fx',
        ),
        isNull,
      );
      expect(pickedFolderLocation('documents'), isNull);
      expect(pickedFolderLocation('file:///music'), isNull);
      expect(
        pickedFolderLocation(
          'content://$externalStorageAuthority/document/primary%3AMusic',
        ),
        isNull,
      );
    });

    test('a collection item is inside a folder of its own volume, below its '
        'path, whatever the case of the names', () {
      final folders = [pickedFolderLocation(treeOf('Music/Library'))!];
      const phone = 'content://media/external_primary/audio/media/7';
      const sd = 'content://media/1a2b-3c4d/audio/media/7';
      expect(
        insidePickedFolder('Music/Library/A/x.flac', phone, folders),
        true,
      );
      expect(insidePickedFolder('music/library/x.flac', phone, folders), true);
      expect(
        insidePickedFolder('Music/Library2/x.flac', phone, folders),
        false,
      );
      expect(insidePickedFolder('Music/x.flac', phone, folders), false);
      expect(insidePickedFolder('Music/Library/x.flac', sd, folders), false);
      expect(
        insidePickedFolder('Ringtones/x.mp3', phone, [
          pickedFolderLocation(treeOf(''))!,
        ]),
        true,
      );
      expect(
        insidePickedFolder('Music/Library/x.flac', 'file:///x', folders),
        false,
      );
    });
  });

  group('the shared collection in the scanner', () {
    late Directory tmp;
    late Directory volume;
    late LibraryDatabase db;
    late StatsDatabase statsDb;
    late DriftTrackStatsStore stats;

    setUp(() {
      tmp = Directory.systemTemp.createTempSync('neupod_mediastore');
      volume = Directory('${tmp.path}/storage')..createSync();
      db = LibraryDatabase(NativeDatabase.memory());
      statsDb = StatsDatabase(NativeDatabase.memory());
      stats = DriftTrackStatsStore(db, statsDb);
    });
    tearDown(() async {
      await db.close();
      await statsDb.close();
      tmp.deleteSync(recursive: true);
    });

    File put(String rel, List<int> bytes) => File('${volume.path}/$rel')
      ..createSync(recursive: true)
      ..writeAsBytesSync(bytes);

    Uint8List flac(List<String> comments) =>
        flacWithComments(fixture('flac16_44k_st.flac'), comments);

    Future<List<Track>> tracks() =>
        DriftLibraryRepository(db).watchSongs().first;

    (FakeMediaStoreApi, FakeMediaStoreStorage, LocalLibraryScanner)
    setUpScanner(FakeFolderAccess access) {
      final api = FakeMediaStoreApi(volume);
      final storage = FakeMediaStoreStorage(api);
      return (
        api,
        storage,
        LocalLibraryScanner(
          folders: access,
          storage: storage,
          db: db,
          onMoves: stats.carry,
        ),
      );
    }

    Future<String> idOf(String title) async =>
        (await tracks()).firstWhere((t) => t.title == title).id.toString();

    test(
      'its songs are read from their files (the tags the device reads), '
      'with paths from the top of the storage and their own source',
      () async {
        put('Music/Album/01 a.flac', flac(['TITLE=A', 'ARTIST=Band']));
        put('Music/Album/cover.jpg', [0xff, 0xd8, 0xff]);
        put('Music/Album/02 untagged.mp3', fixture('mp3_44k_st.mp3'));
        final (_, storage, scanner) = setUpScanner(
          FakeFolderAccess([collection]),
        );
        await scanner.scan().drain<void>();

        final all = await tracks();
        expect(all.map((t) => t.location.relPath), [
          'Music/Album/02 untagged.mp3',
          'Music/Album/01 a.flac',
        ]);
        final a = all.firstWhere((t) => t.title == 'A');
        expect(a.artist, 'Band');
        expect(a.durationMs, greaterThan(0));
        expect(
          a.source,
          const TrackSource(SourceKind.mediaStore, 'mediastore:audio'),
        );
        expect(a.location.uri, startsWith('content://media/external_primary/'));
        // Untagged: the device's fallbacks (owner decision 15).
        expect(all.first.displayTitle, '02 untagged.mp3');
        expect(storage.opened, hasLength(2));
        // No folder art from the collection (owner decision 8).
        final row = await (db.select(db.tracks)).get();
        expect(row.every((r) => r.folderArtUri == null), true);
        final sources = await db.select(db.sources).get();
        expect(sources.single.kind, 'media_store');
      },
    );

    test('a rescan opens only new or changed songs (size and DATE_MODIFIED); '
        'a song gone from MediaStore goes', () async {
      put('Music/a.flac', flac(['TITLE=A']));
      final b = put('Music/b.flac', flac(['TITLE=B']));
      final (api, storage, scanner) = setUpScanner(
        FakeFolderAccess([collection]),
      );
      await scanner.scan().drain<void>();
      expect(storage.opened, hasLength(2));

      storage.opened.clear();
      await scanner.scan().drain<void>();
      expect(storage.opened, isEmpty);

      b.setLastModifiedSync(DateTime(2031));
      put('Music/c.flac', flac(['TITLE=C']));
      await scanner.scan().drain<void>();
      expect(storage.opened, ['Music/b.flac', 'Music/c.flac']);

      api.notMusic.add('Music/a.flac'); // no longer music, as if deleted
      await scanner.scan().drain<void>();
      expect((await tracks()).map((t) => t.title), ['B', 'C']);
    });

    test(
      'a song inside a picked folder is listed once, by the folder',
      () async {
        put('Music/Library/A/a.flac', flac(['TITLE=A']));
        put('Music/Other/b.flac', flac(['TITLE=B']));
        final (_, _, scanner) = setUpScanner(
          FakeFolderAccess([picked('Music/Library'), collection]),
        );
        await scanner.scan().drain<void>();
        final all = await tracks();
        expect(
          {for (final t in all) t.title: (t.source.kind, t.location.relPath)},
          {
            'A': (SourceKind.localFolder, 'A/a.flac'),
            'B': (SourceKind.mediaStore, 'Music/Other/b.flac'),
          },
        );
      },
    );

    test('turning it off removes its songs and nothing else; their ratings '
        'and play counts stay, and come back with it', () async {
      final file = put('Music/a.flac', flac(['TITLE=A']));
      put('Folder/b.flac', flac(['TITLE=B']));
      final access = FakeFolderAccess([picked('Folder'), collection]);
      final (_, _, scanner) = setUpScanner(access);
      await scanner.scan().drain<void>();
      await stats.rate(await idOf('A'), 4);
      await stats.countPlay(await idOf('A'));
      final a = await stats.watch(await idOf('A')).first;

      access.held.removeWhere((f) => f.id == sharedCollectionRootId);
      await scanner.scan().drain<void>();
      expect((await tracks()).map((t) => t.title), ['B']);
      expect(await stats.statsAt('/Music/a.flac', file.lengthSync()), a);
      expect(file.existsSync(), true);

      access.held.add(collection);
      await scanner.scan().drain<void>();
      expect(await stats.watch(await idOf('A')).first, a);
    });

    test('picking a folder over collection songs carries their numbers to '
        "the folder's paths", () async {
      final file = put('Music/Album/a.flac', flac(['TITLE=A']));
      final access = FakeFolderAccess([collection]);
      final (_, _, scanner) = setUpScanner(access);
      await scanner.scan().drain<void>();
      await stats.rate(await idOf('A'), 5);
      final a = await stats.watch(await idOf('A')).first;

      access.held.insert(0, picked('Music'));
      await scanner.scan().drain<void>();
      final t = (await tracks()).single;
      expect(t.source.kind, SourceKind.localFolder);
      expect(t.location.relPath, 'Album/a.flac');
      final size = file.lengthSync();
      expect(await stats.statsAt('/Album/a.flac', size), a);
      expect(await stats.statsAt('/Music/Album/a.flac', size), PlayStats.none);
    });
  });
}
