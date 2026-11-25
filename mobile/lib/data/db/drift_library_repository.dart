// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';

/// [LibraryRepository] over the drift database.
final class DriftLibraryRepository implements LibraryRepository {
  DriftLibraryRepository(this._db);

  final LibraryDatabase _db;

  @override
  Stream<List<Track>> watchSongs() {
    final q =
        _db.select(_db.tracks).join([
          innerJoin(_db.sources, _db.sources.id.equalsExp(_db.tracks.sourceId)),
        ])..orderBy([
          OrderingTerm.asc(_db.tracks.titleSortKey),
          OrderingTerm.asc(_db.tracks.id),
        ]);
    return q.watch().map(
      (rows) => [
        for (final r in rows)
          _track(r.readTable(_db.tracks), r.readTable(_db.sources)),
      ],
    );
  }

  static Track _track(TrackRow t, SourceRow s) => Track(
    id: t.id,
    source: TrackSource(_kind(s.kind), s.uriOrBookmark),
    location: TrackLocation(relPath: t.relPath, uri: t.uri),
    codec: t.codec,
    title: t.title,
    artist: t.artist,
    album: t.album,
    trackNo: t.trackNo,
    durationMs: t.durationMs,
    unreadableReason: UnreadableReason.fromCode(t.reasonCode),
  );

  static SourceKind _kind(String k) => switch (k) {
    'nas' => SourceKind.nas,
    'device' => SourceKind.device,
    _ => SourceKind.localFolder,
  };
}
