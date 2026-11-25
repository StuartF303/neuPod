// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart' show GeneratedDatabase;
import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/db/stats_database.dart';

/// The tables in [db], from SQLite and from drift.
Future<(List<String>, List<String>)> _tables(GeneratedDatabase db) async {
  final rows = await db
      .customSelect(
        "SELECT name FROM sqlite_schema WHERE type = 'table' "
        "AND name NOT LIKE 'sqlite_%'",
      )
      .get();
  return (
    rows.map((r) => r.read<String>('name')).toList(),
    db.allTables.map((t) => t.actualTableName).toList(),
  );
}

void main() {
  // GUARD: the library database holds only what a rescan rebuilds, and phone
  // backups leave it out. Data the user cannot rebuild (ratings, play
  // counts, playlists) does not go here: ratings and play counts are in the
  // stats database below, which backups keep (owner decision 20). Do not
  // widen this list on its own.
  test('the library database has only the sources and tracks tables', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final (sqlite, drift) = await _tables(db);
    expect(sqlite, unorderedEquals(['sources', 'tracks']));
    expect(drift, unorderedEquals(['sources', 'tracks']));
  });

  // GUARD: the stats database (decision 0011) holds the user's own numbers,
  // and nothing a rescan rebuilds (that would go into backups for nothing).
  // A new table comes with its own migration step and test
  // (test/drift/stats/, decision 0008) and a line here saying why.
  test('the stats database has only the track_stats table', () async {
    final db = StatsDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final (sqlite, drift) = await _tables(db);
    expect(sqlite, ['track_stats']);
    expect(drift, ['track_stats']);
  });
}
