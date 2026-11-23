// SPDX-License-Identifier: MPL-2.0

// The lock screen and notification show each song's own art: the queue
// item's artUri is a file:// URI of the art cache's full-size file, made for
// the first song before the queue is set and for the next ones as it plays.

import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/art_providers.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/data/art/art_cache.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod_storage/neupod_storage.dart';

Future<R> _inline<R>(R Function() f) async => f();

void main() {
  late Directory tmp;
  late ArtCache cache;
  late ProviderContainer container;

  setUp(() {
    tmp = Directory.systemTemp.createTempSync('neupod_lock');
    cache = ArtCache(
      directory: () async => '${tmp.path}/art',
      open: (f) async => FileReaderHandle(f.uri),
      run: _inline,
    );
    container = ProviderContainer.test(
      overrides: [artCacheProvider.overrideWithValue(cache)],
    );
  });
  tearDown(() => tmp.deleteSync(recursive: true));

  ArtRef cover(String dir) {
    final f = File('${tmp.path}/music/$dir/Folder.jpg')
      ..createSync(recursive: true)
      ..writeAsBytesSync(
        File('test/fixtures/art/folder_blue.jpg').readAsBytesSync(),
      );
    return ArtRef(
      seed: 1,
      sources: [ArtFile(uri: f.uri.toString(), size: f.lengthSync(), mtime: 1)],
    );
  }

  Song song(String id, ArtRef art, {String? reason}) => Song(
    id: id,
    title: id,
    artist: 'A',
    album: 'B',
    albumId: 'b',
    uri: 'file:///music/$id.flac',
    art: art,
    unplayableReason: reason,
  );

  test(
    'each item carries its own art file; the first is made at once',
    () async {
      final a = cover('a'), b = cover('b');
      final items = await container.read(queueMapperProvider)([
        song('1', a),
        song('2', b),
        song('3', ArtRef.none),
        song('4', a, reason: "Opus, can't play"),
      ], start: 1);
      expect(items.map((i) => i.id), ['1', '2', '3']);
      final uris = items.map((i) => i.artUri).toList();
      expect(uris[0], startsWith('file://'));
      expect(uris[0], endsWith('.f.jpg'));
      expect(uris[1], isNot(uris[0]));
      expect(uris[2], isNull);
      expect(
        File.fromUri(Uri.parse(uris[1]!)).existsSync(),
        isTrue,
        reason: 'the song that starts',
      );
      expect(
        File.fromUri(Uri.parse(uris[0]!)).existsSync(),
        isFalse,
        reason: 'not made before it is needed',
      );
    },
  );

  test(
    'the next songs are made as the queue plays, and after a restart',
    () async {
      final songs = [song('1', cover('a')), song('2', cover('b'))];
      final items = await container.read(queueMapperProvider)(songs);
      final second = File.fromUri(Uri.parse(items[1].artUri!));
      expect(second.existsSync(), isFalse);
      final library = _Songs(songs);
      final state = PlayerState.empty.copyWith(queue: items, index: () => 0);
      await warmLockScreenArt(cache, () async => library, null, state);
      expect(second.existsSync(), isTrue);

      // A new cache over the same directory, as after a restart, with the
      // queue restored: its items' art is found through the library.
      Directory('${tmp.path}/art').deleteSync(recursive: true);
      final restarted = ArtCache(
        directory: () async => '${tmp.path}/art',
        open: (f) async => FileReaderHandle(f.uri),
        run: _inline,
      );
      await warmLockScreenArt(restarted, () async => library, null, state);
      expect(second.existsSync(), isTrue);
      expect(File.fromUri(Uri.parse(items[0].artUri!)).existsSync(), isTrue);
    },
  );
}

/// Only [LibraryView.song].
class _Songs implements LibraryView {
  _Songs(this.list);

  final List<Song> list;

  @override
  Future<Song?> song(String songId) async =>
      list.where((s) => s.id == songId).firstOrNull;

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}
