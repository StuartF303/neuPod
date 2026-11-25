// SPDX-License-Identifier: MPL-2.0

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';

void main() {
  // GUARD: every table of the library database, on purpose. `sources` and
  // `tracks` hold only what a rescan rebuilds. `track_stats` (schema 5,
  // decision 0011) is the first table a rescan cannot rebuild: ratings, play
  // counts and last played. It came with its migration step and data test in
  // test/drift/library/migration_test.dart (decision 0008). A new table
  // comes the same way: its own step, its own test, and a line here saying
  // why. Do not widen this list on its own.
  test(
    'the database has only the sources, tracks and track_stats tables',
    () async {
      final db = LibraryDatabase(NativeDatabase.memory());
      addTearDown(db.close);
      final rows = await db
          .customSelect(
            "SELECT name FROM sqlite_schema WHERE type = 'table' "
            "AND name NOT LIKE 'sqlite_%'",
          )
          .get();
      const tables = ['sources', 'tracks', 'track_stats'];
      expect(rows.map((r) => r.read<String>('name')), unorderedEquals(tables));
      expect(
        db.allTables.map((t) => t.actualTableName),
        unorderedEquals(tables),
      );
    },
  );

  // The user's numbers must outlive the library rows: no foreign key ties
  // them to `tracks` or `sources`, so no scan, removed folder or cascade can
  // delete them.
  test('track_stats has no foreign key', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final keys = await db
        .customSelect("PRAGMA foreign_key_list('track_stats')")
        .get();
    expect(keys, isEmpty);
  });
}
