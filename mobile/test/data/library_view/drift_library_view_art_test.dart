// SPDX-License-Identifier: MPL-2.0

// The library view's art, as the firmware picks it: a song's art is its own
// embedded JPEG, then its folder's cover file; an album's is its first song's
// in album order (the device's album header and Cover Flow ask by the first
// song).

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/library_view/drift_library_view.dart';
import 'package:neupod/domain/library/art_rules.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';

import 'seed.dart';

void main() {
  late LibraryDatabase db;
  late DriftLibraryView view;
  late int src;

  setUp(() async {
    db = LibraryDatabase(NativeDatabase.memory());
    view = DriftLibraryView(db);
    src = await addSource(db);
  });
  tearDown(() async {
    await view.dispose();
    await db.close();
  });

  const cover = 'file:///music/A/Folder.jpg';
  const coverFile = ArtFile(uri: cover, size: 5000, mtime: 7);

  Future<void> add(
    String rel, {
    int trackNo = 0,
    ArtKind kind = ArtKind.none,
    String? folder,
    int? reason,
  }) => addTrack(
    db,
    sourceId: src,
    relPath: rel,
    title: rel,
    artist: 'Band',
    album: 'Album',
    trackNo: trackNo,
    artKind: kind.code,
    artOffset: kind == ArtKind.none ? null : 100,
    artLength: kind == ArtKind.none ? null : 2000,
    folderArtUri: folder,
    folderArtSize: folder == null ? null : 5000,
    folderArtMtime: folder == null ? null : 7,
    reason: reason,
  );

  test('a song with an embedded JPEG and a cover file: embedded first, then '
      'the cover file', () async {
    await add('A/1.flac', trackNo: 1, kind: ArtKind.jpeg, folder: cover);
    final song = (await view.songs()).single;
    expect(song.art.sources, [
      const ArtFile(
        uri: 'file:///music/A/1.flac',
        size: 1,
        mtime: 1,
        offset: 100,
        length: 2000,
      ),
      coverFile,
    ]);
  });

  // Owner decision 7: opening a file iCloud has not put on the phone makes
  // iOS fetch it. A not-downloaded song keeps where its picture is (so it
  // comes back with the file) but offers no source on its own file.
  test('a song not downloaded: no source on its own file; its folder cover '
      'is still offered', () async {
    await add(
      'A/1.flac',
      kind: ArtKind.jpeg,
      folder: cover,
      reason: UnreadableReason.notDownloaded.code,
    );
    final song = (await view.songs()).single;
    expect(song.art.sources, [coverFile]);
    expect(
      song.art.sources.where((s) => s.uri == 'file:///music/A/1.flac'),
      isEmpty,
    );
    final row = (await db.select(db.tracks).get()).single;
    expect((row.artKind, row.artOffset), (ArtKind.jpeg.code, 100));
  });

  test('an embedded PNG is not a source (the device cannot decode it); the '
      'cover file is', () async {
    await add('A/1.flac', kind: ArtKind.png, folder: cover);
    expect((await view.songs()).single.art.sources, [coverFile]);
  });

  test("no picture: no sources, the album's placeholder seed", () async {
    await add('A/1.flac');
    final song = (await view.songs()).single;
    final album = (await view.albums()).single;
    expect(song.art.sources, isEmpty);
    expect(song.art.hasPicture, isFalse);
    expect(song.art.seed, album.art.seed);
  });

  test("an album's art is its first song's in album order, even when a later "
      'song has its own picture', () async {
    await add('A/3.flac', trackNo: 3, kind: ArtKind.jpeg, folder: cover);
    await add('A/1.flac', trackNo: 1, folder: cover);
    await add('A/2.flac', trackNo: 2, kind: ArtKind.jpeg);
    final album = (await view.albums()).single;
    expect(album.art.sources, [coverFile]);
    final detail = await view.album(album.id);
    expect(detail!.songs.first.art, album.art);
    // Each song keeps its own (Now Playing shows the track's picture).
    expect(detail.songs.last.art.sources.first.uri, 'file:///music/A/3.flac');
  });

  test(
    'a row from before schema version 3 has no art until a rescan',
    () async {
      await addTrack(
        db,
        sourceId: src,
        relPath: 'A/1.flac',
        title: 'x',
        album: 'Album',
      );
      expect((await view.albums()).single.art.sources, isEmpty);
    },
  );
}
