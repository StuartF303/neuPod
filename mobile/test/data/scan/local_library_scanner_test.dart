// SPDX-License-Identifier: MPL-2.0

import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:drift/native.dart';
import 'package:flutter/services.dart' show PlatformException;
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/track.dart';
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

  /// Paths under this prefix are left out and the walk reported incomplete,
  /// as when a subfolder cannot be listed or a drive goes away.
  String? unreadablePrefix;

  /// Entries per page; null serves the whole walk as one page.
  int? pageSize;

  /// The platform loses the root mid-walk (a grant revoked, a drive pulled):
  /// after this many pages the next one fails, as `root-unavailable`.
  int? loseRootAfterPages;
  final Map<String, int> _served = {};

  @override
  Future<String> startWalk(String rootId) async {
    final dir = roots[rootId];
    if (dir == null) throw Exception('root-unavailable');
    final entries = <StorageEntry>[];
    for (final f in dir.listSync(recursive: true).whereType<File>()) {
      final rel = f.path.substring(dir.path.length + 1);
      if (unreadablePrefix != null && rel.startsWith(unreadablePrefix!)) {
        continue;
      }
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
    entries.sort((a, b) => a.relPath.compareTo(b.relPath));
    final id = 'w${_n++}';
    _walks[id] = entries;
    return id;
  }

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async {
    final served = _served[walkId] ?? 0;
    if (loseRootAfterPages != null && served >= loseRootAfterPages!) {
      _walks.remove(walkId);
      throw PlatformException(code: 'root-unavailable');
    }
    _served[walkId] = served + 1;
    final rest = _walks[walkId]!;
    final n = pageSize == null || pageSize! > rest.length
        ? rest.length
        : pageSize!;
    final page = rest.sublist(0, n);
    rest.removeRange(0, n);
    final done = rest.isEmpty;
    if (done) _walks.remove(walkId);
    return WalkBatch(
      entries: page,
      done: done,
      complete: done && unreadablePrefix == null,
    );
  }

  @override
  Future<void> cancelWalk(String walkId) async => _walks.remove(walkId);
}

/// Counts handles opened, and can make opens fail.
class CountingStorage extends NeupodStorage {
  CountingStorage(DirApi api) : super(api: api);
  final opened = <String>[];
  final handles = <ReaderHandle>[];
  bool failOpens = false;

  @override
  Future<ReaderHandle> openHandle(StorageEntry entry) async {
    opened.add(entry.relPath.split('/').last);
    if (failOpens) throw StorageException('open-failed');
    final h = await super.openHandle(entry);
    handles.add(h);
    return h;
  }
}

LibraryFolder folder(String id, {bool available = true}) => LibraryFolder(
  id: id,
  name: id,
  kind: FolderKind.pickedFolder,
  available: available,
);

/// A reader whose reads fail with [error].
class FailingReader implements StorageReader {
  FailingReader(this.error);

  final Object error;
  bool closed = false;

  @override
  int get length => 1 << 20;

  @override
  Uint8List readAt(int offset, int count) => throw error;

  @override
  void close() => closed = true;
}

/// Readers that lose access part-way, as `open-failed` (transient). Top
/// level so the scanner can send it to its isolate.
StorageReader accessLostOpener(ReaderHandle h) {
  h.discard();
  return FailingReader(StorageException('open-failed', 'provider went away'));
}

/// Readers whose provider can never serve the file (`unsupported`).
StorageReader unsupportedOpener(ReaderHandle h) {
  h.discard();
  return FailingReader(StorageException('unsupported'));
}

Future<R> failingIsolate<R>(R Function() computation) =>
    Future.error(IsolateSpawnException('no isolate'));

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

  Uint8List flac(List<String> comments) =>
      flacWithComments(fixture('flac16_44k_st.flac'), comments);

  Future<List<Track>> tracks() => DriftLibraryRepository(db).watchSongs().first;

  Future<List<(String, String)>> songs() async => [
    for (final t in await tracks()) (t.displayTitle, t.artistAndAlbum),
  ];

  (DirApi, CountingStorage, LocalLibraryScanner) setUpScanner({
    FakeFolderAccess? access,
    IsolateRunner? runIsolate,
    int version = scanVersion,
    DirApi? api,
    ReaderOpener? openReader,
  }) {
    api ??= DirApi({'m': Directory('${tmp.path}/music')});
    final storage = CountingStorage(api);
    final scanner = LocalLibraryScanner(
      folders: access ?? FakeFolderAccess([folder('m')]),
      storage: storage,
      db: db,
      runIsolate: runIsolate,
      version: version,
      openReader: openReader,
    );
    return (api, storage, scanner);
  }

  test(
    'scans tags, sorts by the firmware rule, skips what is not audio',
    () async {
      put('B/b.flac', flac(['TITLE=The Zebra', 'ARTIST=Band', 'ALBUM=One']));
      put('A/a.mp3', [
        ...id3v2([textFrame('TIT2', 'Apple'), textFrame('TPE1', 'Artist')]),
        ...fixture('mp3_44k_st.mp3'),
      ]);
      put('A/Éclair.flac', fixture('flac16_48k_mono.flac')); // untagged
      put('A/Folder.jpg', [1, 2, 3]);
      put('notes.txt', [1]);
      put('A/10 Track.m4a', [0, 0, 0]); // read, and damaged: listed by name
      put('A/clip.mp4', [0, 0, 0]); // the firmware accepts .mp4 too

      final (_, storage, scanner) = setUpScanner();
      final progress = await scanner.scan().toList();
      expect(progress.last.done, isTrue);
      expect(progress.last.filesFound, 5);
      expect(progress.last.filesRead, 5);

      // Digits first, then letters with accents folded and "The " skipped.
      expect(await songs(), [
        ('10 Track', ''),
        ('Apple', 'Artist'),
        ('clip', ''),
        ('Éclair', ''),
        ('The Zebra', 'Band · One'),
      ]);
      expect(
        storage.opened,
        unorderedEquals([
          'a.mp3',
          'b.flac',
          'Éclair.flac',
          '10 Track.m4a',
          'clip.mp4',
        ]),
      );
    },
  );

  // The firmware keeps what its parser read when the parse fails part-way
  // (`index_file` takes the tags whatever the parse returned): the
  // vectors in test/fixtures/tag_keys show it lists these by their tags.
  test('a file whose parse fails part-way keeps the tags read, with its '
      'reason', () async {
    List<int> vector(String name) =>
        File('test/fixtures/tag_keys/$name').readAsBytesSync();
    put('truncated.flac', vector('truncated.flac'));
    put('no_frames.mp3', vector('no_frames.mp3'));
    final (_, _, scanner) = setUpScanner();
    await scanner.scan().toList();
    final rows = {
      for (final r in await db.select(db.tracks).get()) r.relPath: r,
    };
    final flac = rows['truncated.flac']!;
    expect(flac.title, 'Cut Short');
    expect(flac.artist, 'Kept');
    expect(flac.albumArtist, 'Kept Too');
    expect(flac.trackNo, 4);
    expect(flac.year, 1990);
    expect(flac.genre, 'Blues');
    expect(flac.reasonCode, UnreadableReason.damaged.code);
    final mp3 = rows['no_frames.mp3']!;
    expect(mp3.title, 'Tag Only');
    expect(mp3.albumArtist, 'Host');
    expect(mp3.reasonCode, UnreadableReason.damaged.code);
  });

  test('title sort tags decide the order (owner decision 11)', () async {
    put('a.flac', flac(['TITLE=Aardvark', 'TITLESORT=Zzz']));
    put('b.mp3', [
      ...id3v2([textFrame('TIT2', 'Zulu'), textFrame('TSOT', 'Aaa')]),
      ...fixture('mp3_44k_st.mp3'),
    ]);
    put('c.flac', flac(['TITLE=Middle']));
    final (_, _, scanner) = setUpScanner();
    await scanner.scan().drain<void>();
    final t = await tracks();
    expect(t.map((x) => x.title), ['Zulu', 'Middle', 'Aardvark']);
    final row = await (db.select(
      db.tracks,
    )..where((r) => r.relPath.equals('a.flac'))).getSingle();
    expect(row.titleSort, 'Zzz');
  });

  test(
    'the key uses the firmware truncations; the title is kept whole',
    () async {
      final long = 'B${'x' * 70}'; // past the 63-byte title buffer
      put('1.flac', flac(['TITLE=${long}1']));
      put('2.flac', flac(['TITLE=${long}0']));
      final (_, _, scanner) = setUpScanner();
      await scanner.scan().drain<void>();
      // Both cut to the same 63 bytes, so the same key: the tails the firmware
      // never sees do not order them (ties go by id).
      final rows = await db.select(db.tracks).get();
      expect(rows[0].titleSortKey, rows[1].titleSortKey);
      expect(
        (await tracks()).map((x) => x.title),
        unorderedEquals(['${long}1', '${long}0']),
      );
    },
  );

  test(
    'a rescan reads only new or changed files and drops removed ones',
    () async {
      put('a.flac', flac(['TITLE=A']));
      final gone = put('b.flac', flac(['TITLE=B']));
      final (_, storage, scanner) = setUpScanner();
      await scanner.scan().drain<void>();
      final idA = (await tracks()).first.id;

      storage.opened.clear();
      gone.deleteSync();
      put('c.flac', flac(['TITLE=C']));
      final last = (await scanner.scan().toList()).last;

      expect(storage.opened, ['c.flac']); // a.flac unchanged: not opened
      expect(last.filesRead, 1);
      final t = await tracks();
      expect(t.map((x) => x.title), ['A', 'C']);
      expect(t.first.id, idA); // the row (and its id) survives
    },
  );

  test('an incomplete walk deletes nothing it did not see', () async {
    put('keep/a.flac', flac(['TITLE=A']));
    put('b.flac', flac(['TITLE=B']));
    final (api, _, scanner) = setUpScanner();
    await scanner.scan().drain<void>();
    expect(await songs(), hasLength(2));

    api.unreadablePrefix = 'keep/'; // that folder cannot be listed this time
    await scanner.scan().drain<void>();
    expect((await tracks()).map((t) => t.title), ['A', 'B']);

    api.unreadablePrefix = null; // listed again, and the file really is gone
    File('${tmp.path}/music/keep/a.flac').deleteSync();
    await scanner.scan().drain<void>();
    expect((await tracks()).map((t) => t.title), ['B']);
  });

  test('a transient failure is retried on the next scan', () async {
    put('a.flac', flac(['TITLE=A']));
    final (_, storage, scanner) = setUpScanner();
    storage.failOpens = true;
    await scanner.scan().drain<void>();
    expect(
      (await tracks()).single.unreadableReason,
      UnreadableReason.cannotOpen,
    );

    storage
      ..failOpens = false
      ..opened.clear();
    await scanner.scan().drain<void>(); // same size and mtime, but retried
    expect(storage.opened, ['a.flac']);
    final t = (await tracks()).single;
    expect((t.title, t.unreadableReason), ('A', null));
  });

  test(
    'a lasting failure is not retried while the file is unchanged',
    () async {
      put('bad.flac', Uint8List.fromList(List.filled(100, 7)));
      final (_, storage, scanner) = setUpScanner();
      await scanner.scan().drain<void>();
      storage.opened.clear();
      await scanner.scan().drain<void>();
      expect(storage.opened, isEmpty);
    },
  );

  test(
    'if the isolate cannot start, the handles are closed and the files retried',
    () async {
      put('a.flac', flac(['TITLE=A']));
      final (_, storage, scanner) = setUpScanner(runIsolate: failingIsolate);
      await scanner.scan().drain<void>();
      expect(storage.handles, hasLength(1));
      expect(storage.handles.single.isUsed, isTrue); // discarded
      expect(
        (await tracks()).single.unreadableReason,
        UnreadableReason.cannotRead,
      );
    },
  );

  test('a new scan version reads each file again, once', () async {
    put('a.flac', flac(['TITLE=A']));
    final (_, old, oldScanner) = setUpScanner(version: 1);
    await oldScanner.scan().drain<void>();
    expect(old.opened, ['a.flac']);

    final (_, storage, scanner) = setUpScanner(version: 2);
    await scanner.scan().drain<void>();
    expect(storage.opened, ['a.flac']);
    await scanner.scan().drain<void>();
    expect(storage.opened, ['a.flac']); // not again
  });

  test(
    'an unavailable folder or a partial walk does not make every scan re-read',
    () async {
      put('a.flac', flac(['TITLE=A']));
      put('skip/b.flac', flac(['TITLE=B']));
      final api = DirApi({'m': Directory('${tmp.path}/music')});
      final access = FakeFolderAccess([
        folder('m'),
        folder('gone', available: false),
      ]);
      final (_, old, oldScanner) = setUpScanner(
        access: access,
        version: 1,
        api: api,
      );
      await oldScanner.scan().drain<void>();
      expect(old.opened, unorderedEquals(['a.flac', 'b.flac']));

      api.unreadablePrefix = 'skip/'; // and a subfolder unreadable this time
      final (_, storage, scanner) = setUpScanner(
        access: access,
        version: 2,
        api: api,
      );
      await scanner.scan().drain<void>();
      await scanner.scan().drain<void>();
      expect(storage.opened, ['a.flac']); // once, in spite of both
    },
  );

  test('a transient failure keeps the stored tags', () async {
    put('a.flac', flac(['TITLE=Kept', 'ARTIST=Band']));
    final (_, ok, okScanner) = setUpScanner(version: 1);
    await okScanner.scan().drain<void>();
    expect(ok.opened, ['a.flac']);

    final (_, storage, scanner) = setUpScanner(version: 2);
    storage.failOpens = true;
    await scanner.scan().drain<void>();
    final t = (await tracks()).single;
    expect((t.title, t.artist), ('Kept', 'Band'));
    expect(t.unreadableReason, UnreadableReason.cannotOpen);
  });

  test(
    'a removed folder removes its tracks; an unavailable one keeps them',
    () async {
      put('a.flac', fixture('flac16_44k_st.flac'));
      final access = FakeFolderAccess([folder('m')]);
      final (_, _, scanner) = setUpScanner(access: access);
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

  test(
    'damaged and not-downloaded files are listed with a reason code',
    () async {
      put('bad.flac', Uint8List.fromList(List.filled(100, 7)));
      put('cut.flac', fixture('flac16_44k_st.flac').sublist(0, 20));
      put('icloud/x.mp3', [
        1,
        2,
        3,
      ]); // DirApi marks "cloud" paths not downloaded
      final (_, storage, scanner) = setUpScanner();
      await scanner.scan().drain<void>();

      final reasons = {
        for (final t in await tracks()) t.displayTitle: t.unreadableReason,
      };
      expect(reasons, {
        'bad': UnreadableReason.wrongFormat,
        'cut': UnreadableReason.damaged,
        'x': UnreadableReason.notDownloaded,
      });
      expect(storage.opened, unorderedEquals(['bad.flac', 'cut.flac']));
      final codes = await db
          .customSelect('SELECT reason_code FROM tracks')
          .get();
      expect(
        codes.map((r) => r.read<int>('reason_code')),
        unorderedEquals([4, 5, 1]),
      );
    },
  );

  test('scans never change the music: bytes, modes, mtimes, folders', () async {
    put('a.flac', flac(['TITLE=A']));
    put('sub/b.mp3', fixture('mp3_44k_st.mp3'));
    put('sub/bad.flac', [1, 2, 3]);
    final root = Directory('${tmp.path}/music');
    Map<String, (String?, int, DateTime)> snapshot() => {
      for (final e in root.listSync(recursive: true))
        e.path: (
          e is File ? e.readAsBytesSync().join(',') : null,
          e.statSync().mode,
          e.statSync().modified,
        ),
    };
    final before = snapshot();
    final rootBefore = root.statSync();
    final (_, _, first) = setUpScanner(version: 0);
    await first.scan().drain<void>();
    final (_, _, scanner) = setUpScanner();
    await scanner.scan().drain<void>(); // a full re-read (new version)
    await scanner.scan().drain<void>(); // and a rescan
    expect(snapshot(), before);
    expect(root.statSync().modified, rootBefore.modified);
    expect(root.statSync().mode, rootBefore.mode);
  });

  group('access that goes away and comes back', () {
    /// Rewrites [rel] with a new title and an older mtime, so a rescan sees
    /// it changed.
    void retag(String rel, String title) {
      put(rel, flac(['TITLE=$title'])).setLastModifiedSync(DateTime(2021));
    }

    test('an unavailable folder keeps its tracks; they are re-read when it is back', () async {
      put('a.flac', flac(['TITLE=A']));
      put('b.flac', flac(['TITLE=B']));
      final access = FakeFolderAccess([folder('m')]);
      final (_, storage, scanner) = setUpScanner(access: access);
      await scanner.scan().drain<void>();
      final idA = (await tracks()).first.id;

      access.held
        ..clear()
        ..add(folder('m', available: false));
      retag('a.flac', 'A2');
      File('${tmp.path}/music/b.flac').deleteSync();
      storage.opened.clear();
      await scanner.scan().drain<void>();
      expect(storage.opened, isEmpty);
      expect((await tracks()).map((t) => t.title), ['A', 'B']);

      access.held
        ..clear()
        ..add(folder('m'));
      await scanner.scan().drain<void>();
      expect(storage.opened, ['a.flac']);
      final t = await tracks();
      expect(t.map((x) => x.title), ['A2']); // b really is gone now
      expect(t.single.id, idA); // the same row, updated
    });

    test(
      'access lost mid-walk deletes nothing; files are re-read when it is back',
      () async {
        put('a.flac', flac(['TITLE=A']));
        put('b.flac', flac(['TITLE=B']));
        put('c.flac', flac(['TITLE=C']));
        final (api, storage, scanner) = setUpScanner();
        api.pageSize = 1; // pages: a, b, c
        await scanner.scan().drain<void>();

        retag('a.flac', 'A2'); // on the page served before access goes
        retag('b.flac', 'B2'); // on the page that never comes
        File('${tmp.path}/music/c.flac').deleteSync();
        api.loseRootAfterPages = 1;
        storage.opened.clear();
        final progress = await scanner.scan().toList();
        expect(progress.last.done, isTrue); // the scan itself finishes
        expect(storage.opened, ['a.flac']); // read from the first page
        // That read is stored; b keeps its old tags; c is not deleted.
        expect((await tracks()).map((t) => t.title), ['A2', 'B', 'C']);

        api.loseRootAfterPages = null;
        storage.opened.clear();
        await scanner.scan().drain<void>();
        expect(storage.opened, ['b.flac']);
        expect((await tracks()).map((t) => t.title), ['A2', 'B2']);
      },
    );

    test(
      'an unreadable subfolder keeps its tracks; re-read once it is readable',
      () async {
        put('sub/a.flac', flac(['TITLE=A']));
        put('b.flac', flac(['TITLE=B']));
        final (api, storage, scanner) = setUpScanner();
        await scanner.scan().drain<void>();

        api.unreadablePrefix = 'sub/';
        retag('sub/a.flac', 'A2');
        put('sub/c.flac', flac(['TITLE=C']));
        storage.opened.clear();
        await scanner.scan().drain<void>();
        expect(storage.opened, isEmpty);
        final kept = await tracks();
        expect(kept.map((t) => t.title), ['A', 'B']); // stored tags kept
        expect(kept.first.unreadableReason, isNull);

        api.unreadablePrefix = null;
        await scanner.scan().drain<void>();
        expect(storage.opened, unorderedEquals(['a.flac', 'c.flac']));
        expect((await tracks()).map((t) => t.title), ['A2', 'B', 'C']);
      },
    );
  });

  group('readWithReader', () {
    test('a StorageException while reading is transient; the reader is '
        'closed', () {
      final reader = FailingReader(StorageException('root-unavailable'));
      final (tags, reason) = readWithReader(() => reader, 'flac');
      expect(tags, isNull);
      expect(reason, UnreadableReason.cannotRead);
      expect(reason!.transient, isTrue);
      expect(reader.closed, isTrue);
    });

    test('a StorageException on open is transient', () {
      final (_, reason) = readWithReader(
        () => throw StorageException('open-failed'),
        'mp3',
      );
      expect(reason, UnreadableReason.cannotRead);
    });

    test('an I/O error is transient; anything else is damage', () {
      expect(
        readWithReader(
          () => FailingReader(const FileSystemException('read failed')),
          'flac',
        ).$2,
        UnreadableReason.cannotRead,
      );
      expect(
        readWithReader(() => FailingReader(StateError('odd')), 'flac').$2,
        UnreadableReason.damaged,
      );
    });

    test('StorageException codes: lasting, not downloaded, or transient', () {
      UnreadableReason read(String code) => readWithReader(
        () => FailingReader(StorageException(code)),
        'flac',
      ).$2!;
      expect(read('unsupported'), UnreadableReason.damaged);
      expect(read('unsupported').transient, isFalse);
      expect(read('not-downloaded'), UnreadableReason.notDownloaded);
      for (final code in [
        'open-failed',
        'detached',
        'root-unavailable',
        'busy',
        'channel-error',
        'some-new-code',
      ]) {
        expect(read(code), UnreadableReason.cannotRead, reason: code);
      }
      expect(
        storageReason(StorageException('open-failed'), opening: true),
        UnreadableReason.cannotOpen,
      );
      expect(
        storageReason(StorageException('unsupported'), opening: true),
        UnreadableReason.damaged,
      );
    });
  });

  group('a StorageException in the scan', () {
    test('a transient one keeps the stored tags and is retried', () async {
      put('a.flac', flac(['TITLE=Kept', 'ARTIST=Band']));
      final (_, _, first) = setUpScanner();
      await first.scan().drain<void>();

      // The file changes, but its reads now lose access part-way.
      put(
        'a.flac',
        flac(['TITLE=New', 'ARTIST=Band']),
      ).setLastModifiedSync(DateTime(2021));
      final (api, lost, failing) = setUpScanner(openReader: accessLostOpener);
      await failing.scan().drain<void>();
      expect(lost.opened, ['a.flac']);
      var t = (await tracks()).single;
      expect((t.title, t.artist), ('Kept', 'Band'));
      expect(t.unreadableReason, UnreadableReason.cannotRead);

      // Retried on the next scan, and read this time.
      final (_, storage, scanner) = setUpScanner(api: api);
      await scanner.scan().drain<void>();
      expect(storage.opened, ['a.flac']);
      t = (await tracks()).single;
      expect((t.title, t.unreadableReason), ('New', null));
    });

    test('a lasting one is stored and not retried', () async {
      put('a.flac', flac(['TITLE=A']));
      final (api, _, failing) = setUpScanner(openReader: unsupportedOpener);
      await failing.scan().drain<void>();
      expect(
        (await tracks()).single.unreadableReason,
        UnreadableReason.damaged,
      );
      final (_, storage, scanner) = setUpScanner(api: api);
      await scanner.scan().drain<void>();
      expect(storage.opened, isEmpty);
    });
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
            titleSort: '',
            titleSortKey: Uint8List.fromList(key),
            scanVersion: scanVersion,
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
    final titles = (await tracks()).map((t) => t.title);
    expect(titles, ['short', 'tie', 'long', 'high']);
  });
}
