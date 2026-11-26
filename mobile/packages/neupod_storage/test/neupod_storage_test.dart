// SPDX-License-Identifier: MPL-2.0

import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod_storage/neupod_storage.dart';
import 'package:neupod_storage/src/messages.g.dart';

/// A host API that serves a fixed list of files in pages.
class FakeApi extends NeupodStorageApi {
  FakeApi(this.files);

  final List<StorageEntry> files;
  final calls = <String>[];
  int _cursor = 0;
  PlatformException? failStart;

  /// What the last page reports as `complete`.
  bool complete = true;

  /// When set, a page never comes back (a provider that does not answer).
  bool stall = false;

  /// What [downloadStates] answers, by URI ([DownloadState.missing] if
  /// absent).
  final states = <String, DownloadState>{};
  PlatformException? failDownload;

  @override
  Future<String> startWalk(String rootId) async {
    calls.add('start $rootId');
    if (failStart != null) throw failStart!;
    _cursor = 0;
    return 'w1';
  }

  @override
  Future<WalkBatch> nextWalkBatch(String walkId, int maxEntries) async {
    calls.add('next $walkId $maxEntries');
    if (stall) return Completer<WalkBatch>().future;
    final end = (_cursor + maxEntries).clamp(0, files.length);
    final page = files.sublist(_cursor, end);
    _cursor = end;
    final done = _cursor >= files.length;
    return WalkBatch(entries: page, done: done, complete: done && complete);
  }

  @override
  Future<void> cancelWalk(String walkId) async => calls.add('cancel $walkId');

  @override
  Future<int> openFd(String uri) async {
    calls.add('openFd $uri');
    throw PlatformException(code: 'open-failed', message: 'gone');
  }

  @override
  Future<List<StorageRoot>> listRoots() =>
      Completer<List<StorageRoot>>().future;

  @override
  Future<void> requestDownload(String uri) async {
    calls.add('download $uri');
    if (failDownload != null) throw failDownload!;
  }

  @override
  Future<List<DownloadState>> downloadStates(List<String> uris) async => [
    for (final u in uris) states[u] ?? DownloadState.missing,
  ];

  @override
  Future<String> backedUpDataPath() async => '/data/files/stats';

  @override
  Future<bool> isExcludedFromBackup(String path) async =>
      throw PlatformException(code: 'unsupported', message: 'iOS only');
}

StorageEntry entry(int i, {String? uri, EntryState state = EntryState.local}) =>
    StorageEntry(
      relPath: 'Artist/Album/$i.flac',
      uri: uri ?? 'content://x/$i',
      size: i,
      modifiedMs: 0,
      state: state,
    );

// fopen/fileno/dup/fclose to get a real descriptor on the host, so
// FdStorageReader runs the same pread path as on Android.
typedef _FopenC = Pointer<Void> Function(Pointer<Uint8>, Pointer<Uint8>);
typedef _FilenoC = Int32 Function(Pointer<Void>);
typedef _FilenoD = int Function(Pointer<Void>);
typedef _DupC = Int32 Function(Int32);
typedef _DupD = int Function(int);
typedef _MallocC = Pointer<Uint8> Function(IntPtr);
typedef _MallocD = Pointer<Uint8> Function(int);

int openFdOnHost(String path) {
  final lib = DynamicLibrary.process();
  final fopen = lib.lookupFunction<_FopenC, _FopenC>('fopen');
  final fileno = lib.lookupFunction<_FilenoC, _FilenoD>('fileno');
  final fclose = lib.lookupFunction<_FilenoC, _FilenoD>('fclose');
  final dup = lib.lookupFunction<_DupC, _DupD>('dup');
  final malloc = lib.lookupFunction<_MallocC, _MallocD>('malloc');
  Pointer<Uint8> cString(String s) {
    final bytes = Uint8List.fromList([...s.codeUnits, 0]);
    final p = malloc(bytes.length);
    p.asTypedList(bytes.length).setAll(0, bytes);
    return p;
  }

  final file = fopen(cString(path), cString('r'));
  expect(file, isNot(nullptr));
  final fd = dup(fileno(file));
  fclose(file);
  return fd;
}

void main() {
  group('walk', () {
    test('pages through every file and stops when done', () async {
      final api = FakeApi([for (var i = 0; i < 7; i++) entry(i)]);
      final pages = await NeupodStorage(api: api)
          .walk('root', batchSize: 3)
          .toList();
      expect(pages.map((p) => p.entries.length), [3, 3, 1]);
      expect(pages.map((p) => p.done), [false, false, true]);
      expect(pages.last.complete, isTrue);
      expect(pages.expand((p) => p.entries).map((e) => e.size), [
        0,
        1,
        2,
        3,
        4,
        5,
        6,
      ]);
      expect(api.calls.where((c) => c.startsWith('cancel')), isEmpty);
    });

    test('an empty root yields one empty, complete last page', () async {
      final api = FakeApi([]);
      final pages = await NeupodStorage(api: api).walk('root').toList();
      expect(pages, hasLength(1));
      expect(pages.single.entries, isEmpty);
      expect((pages.single.done, pages.single.complete), (true, true));
    });

    test('an incomplete walk says so on its last page', () async {
      final api = FakeApi([entry(1)])..complete = false;
      final pages = await NeupodStorage(api: api).walk('root').toList();
      expect((pages.last.done, pages.last.complete), (true, false));
    });

    test('cancelling the subscription cancels the walk', () async {
      final api = FakeApi([for (var i = 0; i < 10; i++) entry(i)]);
      final first = await NeupodStorage(api: api)
          .walk('root', batchSize: 2)
          .first;
      expect(first.entries, hasLength(2));
      await pumpEventQueue();
      expect(api.calls, contains('cancel w1'));
    });

    test('a page that never comes back ends the walk with timeout', () async {
      final api = FakeApi([entry(1)])..stall = true;
      await expectLater(
        NeupodStorage(api: api)
            .walk('root', pageTimeout: const Duration(milliseconds: 20))
            .toList(),
        throwsA(
          isA<StorageException>().having((e) => e.code, 'code', 'timeout'),
        ),
      );
      await pumpEventQueue();
      expect(api.calls, contains('cancel w1'));
    });

    test('platform errors become StorageException', () async {
      final api = FakeApi([])
        ..failStart = PlatformException(code: 'root-unavailable');
      await expectLater(
        NeupodStorage(api: api).walk('root').toList(),
        throwsA(
          isA<StorageException>().having(
            (e) => e.code,
            'code',
            'root-unavailable',
          ),
        ),
      );
    });
  });

  group('openReader', () {
    late Directory dir;
    late File file;
    final bytes = Uint8List.fromList(List.generate(1000, (i) => i & 0xff));

    setUp(() {
      dir = Directory.systemTemp.createTempSync('neupod_storage_test');
      file = File('${dir.path}/a.bin')..writeAsBytesSync(bytes);
    });
    tearDown(() => dir.deleteSync(recursive: true));

    void checkReader(StorageReader r) {
      expect(r.length, 1000);
      expect(r.readAt(0, 4), [0, 1, 2, 3]);
      expect(r.readAt(254, 4), [254, 255, 0, 1]);
      expect(r.readAt(998, 10), [998 & 0xff, 999 & 0xff]);
      expect(r.readAt(1000, 10), isEmpty);
      expect(r.readAt(5000, 10), isEmpty);
      expect(r.readAt(10, 0), isEmpty);
      expect(() => r.readAt(-1, 1), throwsRangeError);
    }

    test('file:// URIs are read with dart:io, no platform call', () async {
      final api = FakeApi([]);
      final reader = await NeupodStorage(api: api)
          .openReader(entry(0, uri: file.uri.toString()));
      checkReader(reader);
      reader.close();
      expect(api.calls, isEmpty);
    });

    test('content:// failures become StorageException', () async {
      final api = FakeApi([]);
      await expectLater(
        NeupodStorage(api: api).openReader(entry(1)),
        throwsA(isA<StorageException>()),
      );
      expect(api.calls, ['openFd content://x/1']);
    });

    test('a not-downloaded entry is refused without any call', () async {
      final api = FakeApi([]);
      await expectLater(
        NeupodStorage(api: api)
            .openHandle(entry(1, state: EntryState.notDownloaded)),
        throwsA(
          isA<StorageException>().having(
            (e) => e.code,
            'code',
            'not-downloaded',
          ),
        ),
      );
      expect(api.calls, isEmpty);
    });

    test('a handle is used exactly once', () {
      final h = FdReaderHandle(openFdOnHost(file.path));
      h.discard();
      expect(h.isUsed, isTrue);
      expect(h.discard, throwsStateError);
      expect(h.open, throwsStateError);
      final f = FileReaderHandle(file.uri.toString());
      f.open().close();
      expect(f.open, throwsStateError);
    });

    test('a missing file throws and leaks nothing', () {
      expect(
        () => FileStorageReader.open(Uri.file('${dir.path}/missing')),
        throwsA(isA<FileSystemException>()),
      );
    });

    test('a closed file reader throws on read', () {
      final r = FileStorageReader.open(file.uri)..close();
      r.close();
      expect(() => r.readAt(0, 1), throwsStateError);
    });

    test('FdStorageReader reads with pread and closes once', () {
      final reader = FdStorageReader(openFdOnHost(file.path));
      checkReader(reader);
      reader.close();
      reader.close();
      expect(() => reader.readAt(0, 1), throwsStateError);
    });

    test('handles are opened in another isolate', () async {
      Future<List<int>> headIn(ReaderHandle handle) => Isolate.run(() {
        final r = handle.open();
        try {
          return r.readAt(0, 8);
        } finally {
          r.close();
        }
      });
      final storage = NeupodStorage(api: FakeApi([]));
      final fileHandle = await storage.openHandle(
        entry(0, uri: file.uri.toString()),
      );
      expect(fileHandle, isA<FileReaderHandle>());
      expect(await headIn(fileHandle), [0, 1, 2, 3, 4, 5, 6, 7]);
      expect(await headIn(FdReaderHandle(openFdOnHost(file.path))), [
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
      ]);
      FdReaderHandle(openFdOnHost(file.path)).discard();
    });
  });

  test('roots that never come back end with timeout', () async {
    await expectLater(
      NeupodStorage(api: FakeApi(const []))
          .roots(timeout: const Duration(milliseconds: 20)),
      throwsA(isA<StorageException>().having((e) => e.code, 'code', 'timeout')),
    );
  });

  group('iCloud downloads', () {
    test('a download request goes to the platform with the URI', () async {
      final api = FakeApi(const []);
      await NeupodStorage(api: api).requestDownload('file:///m/a.flac');
      expect(api.calls, ['download file:///m/a.flac']);
    });

    test('a refused download becomes StorageException', () async {
      final api = FakeApi(const [])
        ..failDownload = PlatformException(code: 'download-failed');
      await expectLater(
        NeupodStorage(api: api).requestDownload('file:///m/a.flac'),
        throwsA(
          isA<StorageException>().having(
            (e) => e.code,
            'code',
            'download-failed',
          ),
        ),
      );
    });

    test('states come back in the order asked', () async {
      final api = FakeApi(const [])
        ..states['file:///a'] = DownloadState.downloading
        ..states['file:///b'] = DownloadState.downloaded;
      expect(
        await NeupodStorage(api: api)
            .downloadStates(['file:///b', 'file:///a', 'file:///c']),
        [
          DownloadState.downloaded,
          DownloadState.downloading,
          DownloadState.missing,
        ],
      );
    });
  });

  group('backup paths', () {
    test('the backed-up data path comes from the platform', () async {
      final storage = NeupodStorage(api: FakeApi(const []));
      expect(await storage.backedUpDataPath(), '/data/files/stats');
    });

    test('isExcludedFromBackup errors become StorageException', () async {
      final storage = NeupodStorage(api: FakeApi(const []));
      await expectLater(
        storage.isExcludedFromBackup('/x'),
        throwsA(
          isA<StorageException>().having((e) => e.code, 'code', 'unsupported'),
        ),
      );
    });
  });
}
