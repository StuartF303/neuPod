// SPDX-License-Identifier: MPL-2.0

// The read-only guarantee (README "The library"): the app never writes, moves
// or deletes a music file or folder. This runs the real data layer over a
// generated folder tree and proves it by comparing the tree before and after:
//
// - every entry's kind, size, mtime, mode and (for files) content hash, and
//   the directory listing, are recorded first;
// - a full scan, a rescan, a full re-read (a new scan version), the tag
//   reader's playback-metadata reads and the removal of the folder (whose
//   tracks then go from the library) are run through the scanner and tag
//   reader, with the dart:io-backed folder access the scanner's unit tests
//   use;
// - the recorded state must equal the state afterwards, entry for entry.
//
// The static half of the guarantee (no code that could write a music file)
// is test/read_only_guard_test.dart.

import 'dart:io';
import 'dart:typed_data';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod_storage/neupod_storage.dart';

import '../../fakes/fake_folder_access.dart';
import '../../fixtures/tagged_audio.dart';
import 'local_library_scanner_test.dart' show CountingStorage, DirApi;

/// FNV-1a, 64 bit: a content hash that needs no package. A collision is not a
/// concern when the aim is to notice that a file changed.
int _hash(Uint8List bytes) {
  var h = 0xcbf29ce484222325;
  for (final b in bytes) {
    h = (h ^ b) * 0x100000001b3;
  }
  return h;
}

/// One entry of the tree as the guarantee sees it.
typedef Entry = ({String kind, int size, DateTime mtime, int mode, int? hash});

/// Every file and folder under [root] (and [root] itself, as `.`), by relative
/// path; the key set is the directory listing.
Map<String, Entry> snapshot(Directory root) {
  Entry of(FileSystemEntity e) {
    final s = e.statSync();
    return (
      kind: s.type.toString(),
      size: s.size,
      mtime: s.modified,
      mode: s.mode,
      hash: e is File ? _hash(e.readAsBytesSync()) : null,
    );
  }

  return {
    '.': of(root),
    for (final e in root.listSync(recursive: true, followLinks: false))
      e.path.substring(root.path.length + 1): of(e),
  };
}

final class _ReaderSource implements ByteSource {
  _ReaderSource(this._r);
  final StorageReader _r;
  @override
  int get length => _r.length;
  @override
  Uint8List readAt(int offset, int count) => _r.readAt(offset, count);
}

void main() {
  late Directory tmp;
  late Directory root;
  late LibraryDatabase db;

  File put(String rel, List<int> bytes, {DateTime? mtime}) {
    final f = File('${root.path}/$rel')
      ..createSync(recursive: true)
      ..writeAsBytesSync(bytes);
    if (mtime != null) f.setLastModifiedSync(mtime);
    return f;
  }

  setUp(() {
    tmp = Directory.systemTemp.createTempSync('neupod_readonly');
    root = Directory('${tmp.path}/music')..createSync();
    db = LibraryDatabase(NativeDatabase.memory());
    final flac = fixture('flac16_44k_st.flac');
    final mp3 = fixture('mp3_44k_st.mp3');
    // Old, distinct mtimes: a touched file would show.
    final old = DateTime(2020, 5, 17, 9, 30);
    put(
      'Artist/Album/01 One.flac',
      flacWithComments(flac, ['TITLE=One', 'ARTIST=Artist', 'ALBUM=Album']),
      mtime: old,
    );
    put('Artist/Album/02 Two.mp3', [
      ...id3v2([textFrame('TIT2', 'Two'), textFrame('TPE1', 'Artist')]),
      ...mp3,
    ], mtime: old.add(const Duration(days: 1)));
    put('Artist/Album/Folder.jpg', [0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 4]);
    put('Artist/Album/CD 2/01 Deep.flac', fixture('flac24_96k_st.flac'));
    put('Sigur Rós/Takk…/Hoppípolla [live] #1 & "more".flac', flac);
    put('Various Artists/  spaced name  /ünï cødé 日本語.mp3', mp3);
    put('Various Artists/.hidden.flac', flac);
    put('no extension', [1, 2, 3]);
    put('damaged.flac', Uint8List.fromList(List.filled(64, 7)));
    put('notes.txt', [0x68, 0x69]);
    Directory('${root.path}/Empty folder').createSync();
  });

  tearDown(() async {
    await db.close();
    tmp.deleteSync(recursive: true);
  });

  test('scan, rescan, re-read, metadata reads and folder removal leave the '
      'music untouched', () async {
    final before = snapshot(root);
    expect(before.length, greaterThan(15)); // the tree really is there

    final access = FakeFolderAccess([
      const LibraryFolder(
        id: 'm',
        name: 'Music',
        kind: FolderKind.pickedFolder,
        available: true,
      ),
    ]);
    final api = DirApi({'m': root});
    LocalLibraryScanner scanner({int version = scanVersion}) =>
        LocalLibraryScanner(
          folders: access,
          storage: CountingStorage(api),
          db: db,
          version: version,
        );
    Future<int> tracks() =>
        DriftLibraryRepository(db).watchSongs().first.then((t) => t.length);

    // A full scan.
    await scanner(version: 0).scan().drain<void>();
    expect(await tracks(), greaterThan(5));
    expect(snapshot(root), before, reason: 'after the full scan');

    // A rescan, then a full re-read of every file (a new scan version).
    await scanner(version: 0).scan().drain<void>();
    expect(snapshot(root), before, reason: 'after the rescan');
    await scanner().scan().drain<void>();
    expect(snapshot(root), before, reason: 'after the re-read');

    // Playback metadata: the tags of every audio file, read through the same
    // dart:io reader the scanner uses.
    var read = 0;
    for (final f in root.listSync(recursive: true).whereType<File>()) {
      final ext = f.path.split('.').last;
      if (!const {'flac', 'mp3'}.contains(ext)) continue;
      final reader = FileReaderHandle(f.uri.toString()).open();
      try {
        readTags(_ReaderSource(reader), ext);
        read++;
      } on Object {
        // The damaged file is meant to fail; reading it must not write.
      } finally {
        reader.close();
      }
    }
    expect(read, greaterThan(4));
    expect(snapshot(root), before, reason: 'after the metadata reads');

    // Removing the folder removes its tracks from the library, and only
    // there: the files stay.
    access.held.clear();
    await scanner().scan().drain<void>();
    expect(await tracks(), 0);
    expect(snapshot(root), before, reason: 'after the folder was removed');
  });

  test('the snapshot does notice a change (the test can fail)', () {
    final before = snapshot(root);
    put('notes.txt', [0x68, 0x6f], mtime: DateTime(2020)); // same size
    expect(snapshot(root), isNot(before));
    final again = snapshot(root);
    File('${root.path}/no extension').renameSync('${root.path}/renamed');
    expect(snapshot(root).keys, isNot(again.keys));
  });
}
