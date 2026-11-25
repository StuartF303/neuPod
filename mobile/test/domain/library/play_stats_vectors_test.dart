// SPDX-License-Identifier: MPL-2.0

// Ratings, play counts and "last played" against the firmware itself
// (constitution II). test/fixtures/plays/firmware_vectors.json is printed by
// the firmware's own play_tracker.c, ratings_store.c and plays_store.c,
// compiled for the host by scripts/app/gen-plays-vectors.sh (CI regenerates
// it and fails on a difference):
// - tracker: every pass of scripted and generated runs, with what
//   ptrk_update() said (half heard, 30 s of a track of unknown length, an
//   end after 3 s, the 1 s cap on a held-up pass, paused time, a new track
//   start, a clock that wraps);
// - store: generated sequences of ratings (0 to 7: 6 and 7 are ignored) and
//   records (played or only noted), replayed into the drift store, and every
//   track's stars, plays, last played and stamp afterwards;
// - saturate: 65540 counted plays stop at 65535.

import 'dart:convert';
import 'dart:io';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/drift_track_stats_store.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/play_stats.dart';

final Map<String, Object?> _vectors = jsonDecode(
  File('test/fixtures/plays/firmware_vectors.json').readAsStringSync(),
) as Map<String, Object?>;

void main() {
  group('when a track start counts as a play (ptrk_update)', () {
    final runs = (_vectors['tracker']! as List).cast<Map<String, Object?>>();
    test('the vectors cover every rule', () {
      expect(runs, hasLength(greaterThanOrEqualTo(17)));
      final counted = runs
          .expand((r) => r['passes']! as List)
          .where((p) => (p as List)[5] == 1);
      expect(counted.length, greaterThan(50));
    });
    for (final run in runs) {
      test(run['name']! as String, () {
        final t = PlayTracker();
        for (final (i, p)
            in (run['passes']! as List).cast<List<Object?>>().indexed) {
          final got = t.update(
            nowMs: p[0] as int,
            playing: p[1] == 1,
            ended: p[2] == 1,
            trackId: p[3] as int,
            durationMs: p[4] as int,
          );
          expect(got, p[5] == 1, reason: 'pass $i: $p');
        }
      });
    }
  });

  group('what ratings and records leave (ratings_set, plays_record)', () {
    late LibraryDatabase db;
    late DriftTrackStatsStore store;
    setUp(() {
      db = LibraryDatabase(NativeDatabase.memory());
      store = DriftTrackStatsStore(db);
    });
    tearDown(() => db.close());

    for (final run
        in (_vectors['store']! as List).cast<Map<String, Object?>>()) {
      test(run['name']! as String, () async {
        for (final op in (run['ops']! as List).cast<List<Object?>>()) {
          final path = op[1] as String;
          final size = op[2] as int;
          if (op[0] == 'rate') {
            await store.rateAt(path, size, op[3] as int);
          } else {
            await store.touchAt(path, size, played: op[3] as bool);
          }
        }
        for (final a in (run['after']! as List).cast<List<Object?>>()) {
          expect(
            await store.statsAt(a[0] as String, a[1] as int),
            PlayStats(
              stars: a[2] as int,
              plays: a[3] as int,
              lastPlayed: a[4] as int,
              stamp: a[5] as int,
            ),
            reason: '${a[0]} ${a[1]}',
          );
        }
        // A track with nothing worth keeping has no row.
        final rows = await db.select(db.trackStats).get();
        expect(rows.every((r) => r.rating != 0 || r.stamp != 0), isTrue);
      });
    }
  });

  test('counted plays stop at 65535, as the firmware\'s uint16_t', () {
    final v = _vectors['saturate']! as Map<String, Object?>;
    var s = PlayStats.none;
    var serial = 0;
    for (var i = 0; i < (v['records']! as int); i++) {
      s = s.touched(++serial, played: true);
    }
    expect(s.plays, v['plays']);
    expect(s.plays, maxPlays);
    expect(s.lastPlayed, v['last']);
    expect(s.stamp, v['stamp']);
  });

  test('the drift store stops at 65535 too', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final store = DriftTrackStatsStore(db);
    await store.touchAt('/a.flac', 1, played: true);
    await db.customStatement(
      "UPDATE track_stats SET plays = 65534 WHERE path = '/a.flac'",
    );
    await store.touchAt('/a.flac', 1, played: true);
    await store.touchAt('/a.flac', 1, played: true);
    expect((await store.statsAt('/a.flac', 1)).plays, 65535);
  });

  test('the wheel sets one star per turn, 0 to 5 (p2_scroll)', () {
    expect(starsAfterTurn(0, 1), 1);
    expect(starsAfterTurn(0, 8), 1);
    expect(starsAfterTurn(3, -4), 2);
    expect(starsAfterTurn(5, 1), 5);
    expect(starsAfterTurn(0, -1), 0);
    expect(starsAfterTurn(2, 0), 2);
  });
}
