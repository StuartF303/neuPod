// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/play_stats.dart';

/// The [TrackStatsStore] on the library database's `track_stats` table
/// (decision 0011).
///
/// A track's numbers are keyed as the firmware keys them, by path and size
/// (the `TrackStats` table in `library_database.dart`): a library track's key is
/// `'/' + rel_path` and its size. What a rating or a play does to them is
/// the domain's port of the firmware ([PlayStats.rated],
/// [PlayStats.touched]); each change runs in one transaction, so the serial
/// that orders "last played" is never handed out twice.
///
/// Only this table is written here. Nothing is ever written to a music file,
/// a music folder or a card.
class DriftTrackStatsStore implements TrackStatsStore {
  /// The store on [_db].
  DriftTrackStatsStore(this._db);

  final LibraryDatabase _db;

  /// The track's key, `('/' + rel_path, size)`, or null when the library has
  /// no track [trackId].
  Future<(String, int)?> keyOf(String trackId) async {
    final id = int.tryParse(trackId);
    if (id == null) return null;
    final row = await (_db.select(
      _db.tracks,
    )..where((t) => t.id.equals(id))).getSingleOrNull();
    return row == null ? null : ('/${row.relPath}', row.size);
  }

  @override
  Stream<PlayStats> watch(String trackId) {
    final id = int.tryParse(trackId);
    if (id == null) return Stream.value(PlayStats.none);
    return _db
        .customSelect(
          'SELECT s.rating, s.plays, s.last_played, s.stamp '
          'FROM tracks t JOIN track_stats s '
          "ON s.path = '/' || t.rel_path AND s.size = t.size "
          'WHERE t.id = ?',
          variables: [Variable.withInt(id)],
          readsFrom: {_db.tracks, _db.trackStats},
        )
        .watchSingleOrNull()
        .map(
          (r) => r == null
              ? PlayStats.none
              : PlayStats(
                  stars: r.read<int>('rating'),
                  plays: r.read<int>('plays'),
                  lastPlayed: r.read<int>('last_played'),
                  stamp: r.read<int>('stamp'),
                ),
        );
  }

  @override
  Future<void> rate(String trackId, int stars) async {
    if (stars < 0 || stars > maxStars) return;
    final key = await keyOf(trackId);
    if (key == null) return;
    await _db.transaction(() async {
      await rateAt(key.$1, key.$2, stars);
      // p2_scroll: a well rated track is noted (plays_record, not played).
      if (stars >= noteTrackFromStars) {
        await touchAt(key.$1, key.$2, played: false);
      }
    });
  }

  @override
  Future<void> countPlay(String trackId) async {
    final key = await keyOf(trackId);
    if (key == null) return;
    await touchAt(key.$1, key.$2, played: true);
  }

  /// The numbers kept for [path] of [size] bytes.
  Future<PlayStats> statsAt(String path, int size) async {
    final row = await _row(path, size);
    return row == null ? PlayStats.none : _stats(row);
  }

  /// `ratings_set` for [path] of [size] bytes: 1 to 5 sets the stars, 0
  /// clears them, anything else changes nothing.
  Future<void> rateAt(String path, int size, int stars) =>
      _db.transaction(() async {
        final row = await _row(path, size);
        final was = row == null ? PlayStats.none : _stats(row);
        await _put(path, size, was.rated(stars));
      });

  /// `plays_record` (folded in by `apply`) for [path] of [size] bytes: the
  /// serial counts up, the entry is touched with it and, with [played], a
  /// play is counted.
  Future<void> touchAt(String path, int size, {required bool played}) =>
      _db.transaction(() async {
        final top = _db.trackStats.stamp.max();
        final serial =
            ((await (_db.selectOnly(
                  _db.trackStats,
                )..addColumns([top])).getSingle()).read(top) ??
                0) +
            1;
        final row = await _row(path, size);
        final was = row == null ? PlayStats.none : _stats(row);
        await _put(path, size, was.touched(serial, played: played));
      });

  Future<TrackStatRow?> _row(String path, int size) => (_db.select(
    _db.trackStats,
  )..where((s) => s.path.equals(path) & s.size.equals(size))).getSingleOrNull();

  Future<void> _put(String path, int size, PlayStats s) async {
    if (s.isEmpty) {
      await (_db.delete(
        _db.trackStats,
      )..where((r) => r.path.equals(path) & r.size.equals(size))).go();
      return;
    }
    await _db
        .into(_db.trackStats)
        .insertOnConflictUpdate(
          TrackStatsCompanion.insert(
            path: path,
            size: size,
            rating: Value(s.stars),
            plays: Value(s.plays),
            lastPlayed: Value(s.lastPlayed),
            stamp: Value(s.stamp),
          ),
        );
  }

  static PlayStats _stats(TrackStatRow r) => PlayStats(
    stars: r.rating,
    plays: r.plays,
    lastPlayed: r.lastPlayed,
    stamp: r.stamp,
  );
}
