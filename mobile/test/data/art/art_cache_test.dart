// SPDX-License-Identifier: MPL-2.0

// The art cache: the firmware's order of sources (the embedded JPEG, then the
// cover file; one that does not decode gives way), the two sizes, keys that
// change with the file, the bound on disk, and that the music is only read.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod_storage/neupod_storage.dart';

const _fixtures = 'test/fixtures/art';

/// Runs the isolate's work inline, counting the runs.
class InlineRunner {
  int runs = 0;
  Future<R> call<R>(R Function() computation) async {
    runs++;
    return computation();
  }
}

void main() {
  late Directory tmp;
  late InlineRunner runner;
  late List<String> opened;

  setUp(() {
    tmp = Directory.systemTemp.createTempSync('neupod_art');
    runner = InlineRunner();
    opened = [];
  });
  tearDown(() => tmp.deleteSync(recursive: true));

  ArtCache cache({int maxBytes = 64 << 20}) => ArtCache(
    directory: () async => '${tmp.path}/cache',
    open: (f) async {
      opened.add(f.uri);
      if (!File.fromUri(Uri.parse(f.uri)).existsSync()) {
        throw StorageException('open-failed');
      }
      return FileReaderHandle(f.uri);
    },
    maxBytes: maxBytes,
    run: runner.call,
  );

  /// A copy of a fixture in the "music" folder.
  File music(String name, [String? as]) {
    final f = File('${tmp.path}/music/${as ?? name}')
      ..createSync(recursive: true);
    return f..writeAsBytesSync(File('$_fixtures/$name').readAsBytesSync());
  }

  ArtFile whole(File f) => ArtFile(
    uri: f.uri.toString(),
    size: f.lengthSync(),
    mtime: f.lastModifiedSync().millisecondsSinceEpoch,
  );

  ArtFile embedded(File f) {
    final a = readTags(BytesSource(f.readAsBytesSync()), 'flac').art!;
    return ArtFile(
      uri: f.uri.toString(),
      size: f.lengthSync(),
      mtime: f.lastModifiedSync().millisecondsSinceEpoch,
      offset: a.offset,
      length: a.length,
    );
  }

  /// The colour of the picture's top right (each fixture is two colours
  /// split on the diagonal: red art is red there, blue art blue).
  (int, int, int) topRight(String path) {
    final i = img.decodeJpg(File(path).readAsBytesSync())!;
    final p = i.getPixel(i.width - 4, 3);
    return (p.r.toInt(), p.g.toInt(), p.b.toInt());
  }

  bool reddish((int, int, int) c) => c.$1 > 150 && c.$3 < 100;
  bool bluish((int, int, int) c) => c.$3 > 120 && c.$1 < 100;

  test('the embedded JPEG comes first, read in place', () async {
    final track = music('front_jpeg.flac'); // red
    final cover = music('folder_blue.jpg', 'folder.jpg');
    final art = ArtRef(seed: 1, sources: [embedded(track), whole(cover)]);
    final c = cache();
    final path = await c.file(art, ArtSize.full);
    expect(path, isNotNull);
    expect(reddish(topRight(path!)), isTrue);
    expect(opened, [track.uri.toString()]);
  });

  test('an embedded picture the firmware cannot decode gives way to the '
      'cover file', () async {
    final track = music('progressive.flac');
    final cover = music('cover.jpg'); // red
    final art = ArtRef(seed: 1, sources: [embedded(track), whole(cover)]);
    final path = await cache().file(art, ArtSize.thumb);
    expect(reddish(topRight(path!)), isTrue);
    expect(opened, hasLength(2));
  });

  test('the cover file alone (Folder.jpg)', () async {
    final cover = music('folder_blue.jpg', 'Folder.jpg');
    final path = await cache().file(
      ArtRef(seed: 1, sources: [whole(cover)]),
      ArtSize.full,
    );
    expect(bluish(topRight(path!)), isTrue);
  });

  test('none decodes: null, and not tried again', () async {
    final a = music('progressive.jpg');
    final b = music('png_named.jpg');
    final art = ArtRef(seed: 1, sources: [whole(a), whole(b)]);
    final c = cache();
    expect(await c.file(art, ArtSize.full), isNull);
    expect(runner.runs, 2);
    expect(await c.file(art, ArtSize.thumb), isNull);
    expect(runner.runs, 2);
  });

  test('a source that cannot be opened is tried again next time', () async {
    final missing = ArtFile(
      uri: Uri.file('${tmp.path}/music/gone.jpg').toString(),
      size: 1,
      mtime: 1,
    );
    final art = ArtRef(seed: 1, sources: [missing]);
    final c = cache();
    expect(await c.file(art, ArtSize.full), isNull);
    music('cover.jpg', 'gone.jpg');
    expect(await c.file(art, ArtSize.full), isNotNull);
  });

  test('two sizes, shrunk to fit, never enlarged, never the original', () {
    final big = img.Image(width: 1200, height: 900);
    final full = shrinkToFit(big, ArtSize.full.pixels);
    expect((full.width, full.height), (600, 450));
    final thumb = shrinkToFit(full, ArtSize.thumb.pixels);
    expect((thumb.width, thumb.height), (192, 144));
    final tall = shrinkToFit(img.Image(width: 300, height: 1200), 600);
    expect((tall.width, tall.height), (150, 600));
    final small = img.Image(width: 48, height: 48);
    expect(identical(shrinkToFit(small, 192), small), isTrue);
  });

  test('both files are made at once, and a hit reads nothing', () async {
    final cover = music('cover.jpg');
    final art = ArtRef(seed: 1, sources: [whole(cover)]);
    final c = cache();
    final full = await c.file(art, ArtSize.full);
    final thumb = await c.file(art, ArtSize.thumb);
    expect(full, isNot(thumb));
    expect(File(full!).existsSync() && File(thumb!).existsSync(), isTrue);
    expect(runner.runs, 1);
    expect(opened, hasLength(1));
    // The 48 px fixture is not enlarged.
    expect(img.decodeJpg(File(full).readAsBytesSync())!.width, 48);
  });

  test('a changed file is a new key', () async {
    final cover = music('cover.jpg');
    final before = ArtRef(seed: 1, sources: [whole(cover)]);
    final c = cache();
    final p1 = await c.file(before, ArtSize.full);
    music('folder_blue.jpg', 'cover.jpg');
    cover.setLastModifiedSync(DateTime(2031));
    final after = ArtRef(seed: 1, sources: [whole(cover)]);
    expect(after.cacheKey, isNot(before.cacheKey));
    final p2 = await c.file(after, ArtSize.full);
    expect(p2, isNot(p1));
    expect(bluish(topRight(p2!)), isTrue);
    expect(reddish(topRight(p1!)), isTrue);
  });

  test('a cover file added to a folder is new art too', () {
    final track = music('no_picture.flac');
    final a = ArtRef(seed: 1, sources: [whole(track)]);
    final b = ArtRef(
      seed: 1,
      sources: [whole(track), whole(music('cover.jpg'))],
    );
    expect(a, isNot(b));
    expect(
      ArtCache.fileName(a, ArtSize.full),
      isNot(ArtCache.fileName(b, ArtSize.full)),
    );
  });

  test(
    'the cache stays under its bound, least recently used out first',
    () async {
      final c = cache(maxBytes: 6000);
      final paths = <String>[];
      for (var i = 0; i < 6; i++) {
        final f = music('cover.jpg', 'a$i/cover.jpg');
        final p = await c.file(
          ArtRef(seed: i, sources: [whole(f)]),
          ArtSize.full,
        );
        File(p!).setLastModifiedSync(DateTime(2020, 1, 1 + i));
        paths.add(p);
      }
      await c.evictNow();
      var total = 0;
      for (final f in Directory(
        '${tmp.path}/cache',
      ).listSync().whereType<File>()) {
        total += f.lengthSync();
      }
      expect(total, lessThanOrEqualTo(6000));
      expect(File(paths.first).existsSync(), isFalse, reason: 'oldest first');
      expect(File(paths.last).existsSync(), isTrue);
    },
  );

  test('warm makes a path pathFor gave, once', () async {
    final cover = music('cover.jpg');
    final art = ArtRef(seed: 1, sources: [whole(cover)]);
    final c = cache();
    final p = await c.pathFor(art, ArtSize.full);
    expect(File(p!).existsSync(), isFalse);
    await c.warm([p, p, '/elsewhere.jpg']);
    expect(File(p).existsSync(), isTrue);
    expect(runner.runs, 1);
  });

  test('the music is only read', () async {
    final track = music('front_jpeg.flac');
    final before = track.readAsBytesSync();
    final mtime = track.lastModifiedSync();
    await cache().file(
      ArtRef(seed: 1, sources: [embedded(track)]),
      ArtSize.full,
    );
    expect(track.readAsBytesSync(), before);
    expect(track.lastModifiedSync(), mtime);
    expect(
      Directory('${tmp.path}/music')
          .listSync()
          .map((e) => e.path.split('/').last),
      ['front_jpeg.flac'],
    );
  });

  test('a ready path passes through; no sources and no path is null', () async {
    final c = cache();
    expect(
      await c.file(const ArtRef(seed: 1, path: '/x.jpg'), ArtSize.full),
      '/x.jpg',
    );
    expect(await c.file(ArtRef.none, ArtSize.full), isNull);
  });
}
