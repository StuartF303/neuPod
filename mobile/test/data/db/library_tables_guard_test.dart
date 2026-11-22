// SPDX-License-Identifier: MPL-2.0

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';

void main() {
  // GUARD: the library database holds only what a rescan rebuilds. Update this
  // test in the same PR that adds the first user-owned table (ratings, play
  // counts, playlists), and that PR must come with a real migration step and
  // its data test in test/drift/library/migration_test.dart (decision 0008).
  // Do not widen this list on its own.
  test('the database has only the sources and tracks tables', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final rows = await db
        .customSelect(
          "SELECT name FROM sqlite_schema WHERE type = 'table' "
          "AND name NOT LIKE 'sqlite_%'",
        )
        .get();
    expect(
      rows.map((r) => r.read<String>('name')),
      unorderedEquals(['sources', 'tracks']),
    );
    expect(
      db.allTables.map((t) => t.actualTableName),
      unorderedEquals(['sources', 'tracks']),
    );
  });
}
