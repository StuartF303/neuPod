// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/db/stats_database.dart';
import 'package:neupod/domain/library/play_stats.dart';

/// The [TrackStatsStore] on the stats database's `track_stats` table
/// (decision 0011), finding tracks in the library database.
///
/// A track's numbers are keyed as the firmware keys them, by path and size
/// (the `TrackStats` table in `stats_database.dart`): a library track's key
/// is `'/' + rel_path` and its size. What a rating or a play does to them is
/// the domain's port of the firmware ([PlayStats.rated],
/// [PlayStats.touched]); each change runs in one transaction, so the serial
/// that orders "last played" is never handed out twice.
///
/// Only the stats database is written here, and only its one table. Nothing
/// is ever written to a music file, a music folder or a card.
class DriftTrackStatsStore implements TrackStatsStore {
  /// The store on [_stats], with tracks looked up in [_library].
  DriftTrackStatsStore(this._library, this._stats);

  final LibraryDatabase _library;
  final StatsDatabase _stats;

  /// The track's key, `('/' + rel_path, size)`, or null when the library has
  /// no track [trackId].
  Future<(String, int)?> keyOf(String trackId) async {
    final id = int.tryParse(trackId);
    if (id == null) return null;
    final row = await _trackQuery(id).getSingleOrNull();
    return row == null ? null : _keyOfRow(row);
  }

  SimpleSelectStatement<$TracksTable, TrackRow> _trackQuery(int id) =>
      _library.select(_library.tracks)..where((t) => t.id.equals(id));

  static (String, int) _keyOfRow(TrackRow r) => ('/${r.relPath}', r.size);

  /// Follows the track's key (a rescan may change it: a retag changes the
  /// size) and, for each key, its numbers.
  @override
  Stream<PlayStats> watch(String trackId) {
    final id = int.tryParse(trackId);
    if (id == null) return Stream.value(PlayStats.none);
    final keys = _trackQuery(id)
        .watchSingleOrNull()
        .map((r) => r == null ? null : _keyOfRow(r));
    return _switch(keys.distinct(), (key) {
      if (key == null) return Stream.value(PlayStats.none);
      return _rowQuery(key.$1, key.$2).watchSingleOrNull().map(
        (r) => r == null ? PlayStats.none : _statsOf(r),
      );
    });
  }

  @override
  Future<void> rate(String trackId, int stars) async {
    if (stars < 0 || stars > maxStars) return;
    final key = await keyOf(trackId);
    if (key == null) return;
    await _stats.transaction(() async {
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

  /// Carries each move's numbers to its new key, in one transaction. A move
  /// whose old key has no numbers, or whose new key already has some (they
  /// are never overwritten), changes nothing.
  @override
  Future<void> carry(List<TrackMove> moves) => _stats.transaction(() async {
    for (final m in moves) {
      if (m.fromPath == m.toPath && m.fromSize == m.toSize) continue;
      final from = await _row(m.fromPath, m.fromSize);
      if (from == null || await _row(m.toPath, m.toSize) != null) continue;
      await _put(m.toPath, m.toSize, _statsOf(from));
      await _put(m.fromPath, m.fromSize, PlayStats.none);
    }
  });

  /// The numbers kept for [path] of [size] bytes.
  Future<PlayStats> statsAt(String path, int size) async {
    final row = await _row(path, size);
    return row == null ? PlayStats.none : _statsOf(row);
  }

  /// `ratings_set` for [path] of [size] bytes: 1 to 5 sets the stars, 0
  /// clears them, anything else changes nothing.
  Future<void> rateAt(String path, int size, int stars) =>
      _stats.transaction(() async {
        final row = await _row(path, size);
        final was = row == null ? PlayStats.none : _statsOf(row);
        await _put(path, size, was.rated(stars));
      });

  /// `plays_record` (folded in by `apply`) for [path] of [size] bytes: the
  /// serial counts up, the entry is touched with it and, with [played], a
  /// play is counted.
  Future<void> touchAt(String path, int size, {required bool played}) =>
      _stats.transaction(() async {
        final top = _stats.trackStats.stamp.max();
        final serial =
            ((await (_stats.selectOnly(
                  _stats.trackStats,
                )..addColumns([top])).getSingle()).read(top) ??
                0) +
            1;
        final row = await _row(path, size);
        final was = row == null ? PlayStats.none : _statsOf(row);
        await _put(path, size, was.touched(serial, played: played));
      });

  SimpleSelectStatement<$TrackStatsTable, TrackStatRow> _rowQuery(
    String path,
    int size,
  ) =>
      _stats.select(_stats.trackStats)
        ..where((s) => s.path.equals(path) & s.size.equals(size));

  Future<TrackStatRow?> _row(String path, int size) =>
      _rowQuery(path, size).getSingleOrNull();

  Future<void> _put(String path, int size, PlayStats s) async {
    if (s.isEmpty) {
      await (_stats.delete(
        _stats.trackStats,
      )..where((r) => r.path.equals(path) & r.size.equals(size))).go();
      return;
    }
    await _stats
        .into(_stats.trackStats)
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

  static PlayStats _statsOf(TrackStatRow r) => PlayStats(
    stars: r.rating,
    plays: r.plays,
    lastPlayed: r.lastPlayed,
    stamp: r.stamp,
  );
}

/// Each value of [outer] mapped to a stream, following only the latest.
Stream<R> _switch<T, R>(Stream<T> outer, Stream<R> Function(T) inner) {
  late StreamController<R> out;
  StreamSubscription<T>? outerSub;
  StreamSubscription<R>? innerSub;
  out = StreamController<R>(
    onListen: () {
      outerSub = outer.listen(
        (v) {
          innerSub?.cancel();
          innerSub = inner(v).listen(out.add, onError: out.addError);
        },
        onError: out.addError,
        onDone: () async {
          await innerSub?.asFuture<void>().catchError((_) {});
          await out.close();
        },
      );
    },
    onPause: () {
      outerSub?.pause();
      innerSub?.pause();
    },
    onResume: () {
      outerSub?.resume();
      innerSub?.resume();
    },
    onCancel: () async {
      await innerSub?.cancel();
      await outerSub?.cancel();
    },
  );
  return out.stream;
}
