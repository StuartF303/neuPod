// SPDX-License-Identifier: MPL-2.0

import 'dart:io';
import 'dart:typed_data';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod_storage/neupod_storage.dart';
// ignore: implementation_imports
import 'package:neupod_storage/src/messages.g.dart';

import '../../fakes/fake_folder_access.dart';
import '../../fixtures/tagged_audio.dart';

/// Serves a real directory on the host as a storage root: entries carry
/// `file://` URIs, so the scanner reads them through the same
/// `FileReaderHandle` path as on iOS.
class DirApi extends NeupodStorageApi {
  DirApi(this.roots);

  final Map<String, Directory> roots;
  final Map<String, List<StorageEntry>> _walks = {};
  int _n = 0;

  @override
  Future<String> startWalk(String rootId) async {
    final dir = roots[rootId];
    if (dir == null) throw Exception('root-unavailable');
    final entries = <StorageEntry>[];
    for (final f in dir.listSync(recursive: true).whereType<File>()) {
      final rel = f.path.substring(dir.path.length + 1);
      entries.add(
        StorageEntry(
          relPath: rel,
          uri: f.uri.toString(),
          size: f.lengthSync(),
          modifiedMs: f.lastModifiedSync().millisecondsSinceEpoch,
          state: rel.contains('cloud')
              ? EntryState.notDownloaded
              : EntryState.local,
        ),
      );
    }
    final id = 'w${_n++}';
    _walks[id] = entries;
    return id;
  }

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async {
    final all = _walks.remove(walkId)!;
    return WalkBatch(entries: all, done: true);
  }

  @override
  Future<void> cancelWalk(String walkId) async => _walks.remove(walkId);
}

/// Counts handles opened, by wrapping NeupodStorage.
class CountingStorage extends NeupodStorage {
  CountingStorage(DirApi api) : super(api: api);
  final opened = <String>[];

  @override
  Future<ReaderHandle> openHandle(String uri) {
    opened.add(uri.split('/').last);
    return super.openHandle(uri);
  }
}

LibraryFolder folder(String id, {bool available = true}) => LibraryFolder(
  id: id,
  name: id,
  kind: FolderKind.pickedFolder,
  available: available,
);

void main() {
  late Directory tmp;
  late LibraryDatabase db;

  setUp(() {
    tmp = Directory.systemTemp.createTempSync('neupod_scan');
    db = LibraryDatabase(NativeDatabase.memory());
  });
  tearDown(() async {
    await db.close();
    tmp.deleteSync(recursive: true);
  });

  File put(String rel, List<int> bytes) => File('${tmp.path}/music/$rel')
    ..createSync(recursive: true)
    ..writeAsBytesSync(bytes);

  Future<List<(String, String)>> songs() async => [
    for (final t in await DriftLibraryRepository(db).watchSongs().first)
      (t.displayTitle, t.artistAndAlbum),
  ];

  test(
    'scans tags, sorts by the firmware rule, skips what is not audio',
    () async {
      put(
        'B/b.flac',
        flacWithComments(fixture('flac16_44k_st.flac'), [
          'TITLE=The Zebra',
          'ARTIST=Band',
          'ALBUM=One',
        ]),
      );
      put('A/a.mp3', [
        ...id3v2([textFrame('TIT2', 'Apple'), textFrame('TPE1', 'Artist')]),
        ...fixture('mp3_44k_st.mp3'),
      ]);
      put('A/Éclair.flac', fixture('flac16_48k_mono.flac')); // untagged
      put('A/Folder.jpg', [1, 2, 3]);
      put('notes.txt', [1]);
      put('A/10 Track.m4a', [0, 0, 0]); // listed by name until phase 3

      final storage = CountingStorage(
        DirApi({'m': Directory('${tmp.path}/music')}),
      );
      final scanner = LocalLibraryScanner(
        folders: FakeFolderAccess([folder('m')]),
        storage: storage,
        db: db,
      );
      final progress = await scanner.scan().toList();
      expect(progress.last.done, isTrue);
      expect(progress.last.filesFound, 4);
      expect(progress.last.filesRead, 4);

      // Digits first, then letters with accents folded and "The " skipped.
      expect(await songs(), [
        ('10 Track', ''),
        ('Apple', 'Artist'),
        ('Éclair', ''),
        ('The Zebra', 'Band · One'),
      ]);
      expect(
        storage.opened,
        unorderedEquals(['a.mp3', 'b.flac', '%C3%89clair.flac']),
      );
    },
  );

  test(
    'a rescan reads only new or changed files and drops removed ones',
    () async {
      put(
        'a.flac',
        flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=A']),
      );
      final gone = put(
        'b.flac',
        flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=B']),
      );
      final storage = CountingStorage(
        DirApi({'m': Directory('${tmp.path}/music')}),
      );
      final scanner = LocalLibraryScanner(
        folders: FakeFolderAccess([folder('m')]),
        storage: storage,
        db: db,
      );
      await scanner.scan().drain<void>();
      final idA = (await DriftLibraryRepository(
        db,
      ).watchSongs().first).first.id;

      storage.opened.clear();
      gone.deleteSync();
      put(
        'c.flac',
        flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=C']),
      );
      final last = (await scanner.scan().toList()).last;

      expect(storage.opened, ['c.flac']); // a.flac unchanged: not opened
      expect(last.filesRead, 1);
      final tracks = await DriftLibraryRepository(db).watchSongs().first;
      expect(tracks.map((t) => t.title), ['A', 'C']);
      expect(tracks.first.id, idA); // the row (and its id) survives
    },
  );

  test(
    'a removed folder removes its tracks; an unavailable one keeps them',
    () async {
      put('a.flac', fixture('flac16_44k_st.flac'));
      final access = FakeFolderAccess([folder('m')]);
      final scanner = LocalLibraryScanner(
        folders: access,
        storage: NeupodStorage(
          api: DirApi({'m': Directory('${tmp.path}/music')}),
        ),
        db: db,
      );
      await scanner.scan().drain<void>();
      expect(await songs(), hasLength(1));

      access.held
        ..clear()
        ..add(folder('m', available: false));
      await scanner.scan().drain<void>();
      expect(await songs(), hasLength(1));

      access.held.clear();
      await scanner.scan().drain<void>();
      expect(await songs(), isEmpty);
    },
  );

  test('damaged and not-downloaded files are listed with a reason', () async {
    put('bad.flac', Uint8List.fromList(List.filled(100, 7)));
    put('icloud/x.mp3', [1, 2, 3]); // DirApi marks "cloud" paths not downloaded
    final storage = CountingStorage(
      DirApi({'m': Directory('${tmp.path}/music')}),
    );
    await LocalLibraryScanner(
      folders: FakeFolderAccess([folder('m')]),
      storage: storage,
      db: db,
    ).scan().drain<void>();

    final tracks = await DriftLibraryRepository(db).watchSongs().first;
    final reasons = {
      for (final t in tracks) t.displayTitle: t.unreadableReason,
    };
    expect(reasons, {'bad': 'not a FLAC file', 'x': 'Not downloaded'});
    expect(storage.opened, ['bad.flac']); // never opened the evicted file
  });

  test('the music files are not modified by a scan', () async {
    final f = put(
      'a.flac',
      flacWithComments(fixture('flac16_44k_st.flac'), ['TITLE=A']),
    );
    final before = (f.readAsBytesSync(), f.lastModifiedSync());
    await LocalLibraryScanner(
      folders: FakeFolderAccess([folder('m')]),
      storage: NeupodStorage(
        api: DirApi({'m': Directory('${tmp.path}/music')}),
      ),
      db: db,
    ).scan().drain<void>();
    expect(f.readAsBytesSync(), before.$1);
    expect(f.lastModifiedSync(), before.$2);
    expect(
      Directory('${tmp.path}/music').listSync(recursive: true),
      hasLength(1),
    );
  });

  test('extensionOf', () {
    expect(extensionOf('a/b.FLAC'), 'flac');
    expect(extensionOf('a.b/c'), '');
    expect(extensionOf('.hidden'), '');
  });

  test('songs are ordered by the BLOB key in SQL, ties by id', () async {
    final src = await db
        .into(db.sources)
        .insert(
          SourcesCompanion.insert(
            kind: 'local_folder',
            uriOrBookmark: 's',
            displayName: 's',
          ),
        );
    Future<void> add(String rel, String title, List<int> key) => db
        .into(db.tracks)
        .insert(
          TracksCompanion.insert(
            sourceId: src,
            relPath: rel,
            uri: rel,
            size: 0,
            mtime: 0,
            codec: 'flac',
            title: title,
            titleSortKey: Uint8List.fromList(key),
            artist: '',
            album: '',
            trackNo: 0,
            durationMs: 0,
            sampleRate: 0,
            bitDepth: 0,
          ),
        );
    await add('1', 'long', [0xC1, 0xC0]);
    await add('2', 'short', [0xC1]); // a prefix sorts first, as memcmp
    await add('3', 'high', [0xFF]);
    await add('4', 'tie', [0xC1]);
    final titles = (await DriftLibraryRepository(
      db,
    ).watchSongs().first).map((t) => t.title);
    expect(titles, ['short', 'tie', 'long', 'high']);
  });
}
