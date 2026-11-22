// SPDX-License-Identifier: MPL-2.0

// Migration tests for the library database (decision 0008), on drift's
// generated schema verifier. generated/ is written by
// `dart run drift_dev make-migrations` from drift_schemas/library/; do not edit
// it by hand.
//
// Every version must migrate from each earlier one:
// - to the exact schema of its snapshot (the simple tests), and
// - with every row kept (the data tests): a row is put in each table of the
//   old version, and after the migration each of those tables still has it,
//   with the same value in every column that is still there.
// A step that drops a column on purpose lists it in [_droppedOnPurpose].

import 'package:drift/drift.dart';
import 'package:drift/native.dart';
import 'package:drift_dev/api/migrations_native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:sqlite3/sqlite3.dart' as sql;

import 'generated/schema.dart';

/// Columns a migration removes on purpose, as `'table.column'`, keyed by the
/// version that removes them. Empty: no column has been removed.
const _droppedOnPurpose = <int, Set<String>>{};

const _versions = GeneratedHelper.versions;

void main() {
  driftRuntimeOptions.dontWarnAboutMultipleDatabases = true;
  late SchemaVerifier verifier;

  setUpAll(() {
    verifier = SchemaVerifier(GeneratedHelper());
  });

  test(
    'the current schema version has a snapshot, and it is the newest',
    () async {
      final db = LibraryDatabase(NativeDatabase.memory());
      expect(_versions.last, db.schemaVersion);
      await db.close();
    },
  );

  test('a new database matches the snapshot of the current version', () async {
    final db = LibraryDatabase(await verifier.startAt(_versions.last));
    // Opens at the same version (no migration) and compares sqlite_schema with
    // the snapshot: a table changed without a new version fails here.
    await verifier.migrateAndValidate(db, _versions.last);
    await db.close();
  });

  group('simple migrations', () {
    for (final (i, from) in _versions.indexed) {
      for (final to in _versions.skip(i + 1)) {
        test('from $from to $to', () async {
          final schema = await verifier.schemaAt(from);
          final db = LibraryDatabase(schema.newConnection());
          await verifier.migrateAndValidate(db, to);
          await db.close();
        });
      }
    }
  });

  group('migrations keep every row', () {
    final current = _versions.last;
    for (final from in _versions.where((v) => v < current)) {
      test('from $from to $current', () async {
        await _expectRowsKept(verifier, from, current);
      });
    }
    // Runs the same check with no migration, so the helper is known to work
    // before the first real step needs it.
    test('the check itself (at version $current, no migration)', () async {
      await _expectRowsKept(verifier, current, current);
    });
  });

  test('a database from a newer build is refused, not dropped', () async {
    final schema = await verifier.schemaAt(_versions.last);
    final rows = _fillEveryTable(schema.rawDatabase, _versions.last);
    schema.rawDatabase.execute('PRAGMA user_version = ${_versions.last + 1}');

    final db = LibraryDatabase(schema.newConnection());
    await expectLater(
      db.customSelect('SELECT 1').get(),
      throwsA(isA<StateError>()),
    );
    await db.close();
    for (final table in rows.keys) {
      expect(
        schema.rawDatabase.select('SELECT * FROM "$table"'),
        hasLength(1),
        reason: '$table was changed by a refused downgrade',
      );
    }
  });
}

/// Puts one row in every table of [from], migrates to [to], and checks each
/// row is still there with its values.
Future<void> _expectRowsKept(SchemaVerifier verifier, int from, int to) async {
  final schema = await verifier.schemaAt(from);
  final before = _fillEveryTable(schema.rawDatabase, from);

  final db = LibraryDatabase(schema.newConnection());
  await verifier.migrateAndValidate(db, to);
  final dropped = {
    for (final v in _droppedOnPurpose.keys)
      if (v > from && v <= to) ..._droppedOnPurpose[v]!,
  };
  for (final MapEntry(key: table, value: row) in before.entries) {
    final after = await db.customSelect('SELECT * FROM "$table"').get();
    expect(after, hasLength(1), reason: 'rows of $table after $from to $to');
    final now = after.single.data;
    for (final MapEntry(key: column, value: value) in row.entries) {
      if (dropped.contains('$table.$column')) continue;
      expect(
        now.containsKey(column),
        isTrue,
        reason:
            '$table.$column is gone after $from to $to; if on purpose, '
            'list it in _droppedOnPurpose',
      );
      expect(now[column], value, reason: '$table.$column after $from to $to');
    }
  }
  await db.close();
}

/// Inserts one row into every table of the raw database, with a value of the
/// declared type in every column (integers are 1, so foreign keys point at
/// the row of the parent table), and returns what was written.
Map<String, Map<String, Object?>> _fillEveryTable(sql.Database raw, int v) {
  final written = <String, Map<String, Object?>>{};
  final tables = raw.select(
    "SELECT name FROM sqlite_schema WHERE type = 'table' "
    "AND name NOT LIKE 'sqlite_%' ORDER BY name",
  );
  for (final t in tables) {
    final table = t['name'] as String;
    final row = <String, Object?>{};
    for (final c in raw.select('PRAGMA table_info("$table")')) {
      final type = (c['type'] as String).toUpperCase();
      final name = c['name'] as String;
      row[name] = switch (type) {
        'INTEGER' => 1,
        'REAL' => 1.5,
        'BLOB' => Uint8List.fromList([v, 0xC1, 0xFF]),
        _ => 'v$v $table.$name',
      };
    }
    final columns = row.keys.map((c) => '"$c"').join(', ');
    final marks = List.filled(row.length, '?').join(', ');
    raw.execute(
      'INSERT INTO "$table" ($columns) VALUES ($marks)',
      row.values.toList(),
    );
    written[table] = row;
  }
  return written;
}
