// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/library/play_stats.dart';

/// A [TrackStatsStore] in memory, keyed by track id, for widget tests that
/// do not need the database. It applies the domain's rules as the drift
/// store does and records every call.
class FakeTrackStatsStore implements TrackStatsStore {
  final Map<String, PlayStats> stats = {};
  final calls = <String>[];
  final _changes = StreamController<void>.broadcast();
  int _serial = 0;

  @override
  Stream<PlayStats> watch(String trackId) async* {
    yield stats[trackId] ?? PlayStats.none;
    await for (final _ in _changes.stream) {
      yield stats[trackId] ?? PlayStats.none;
    }
  }

  @override
  Future<void> rate(String trackId, int stars) async {
    calls.add('rate($trackId, $stars)');
    if (stars < 0 || stars > maxStars) return;
    var s = (stats[trackId] ?? PlayStats.none).rated(stars);
    if (stars >= noteTrackFromStars) s = s.touched(++_serial, played: false);
    stats[trackId] = s;
    _changes.add(null);
  }

  @override
  Future<void> countPlay(String trackId) async {
    calls.add('countPlay($trackId)');
    stats[trackId] = (stats[trackId] ?? PlayStats.none).touched(
      ++_serial,
      played: true,
    );
    _changes.add(null);
  }

  @override
  Future<void> carry(List<TrackMove> moves) async =>
      calls.add('carry(${moves.length})');
}
