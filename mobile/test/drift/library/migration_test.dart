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

import 'package:drift/drift.dart' hide isNotNull, isNull;
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

  test('1 to 2: tracks keep their rows and get no firmware text until a '
      'rescan', () async {
    final schema = await verifier.schemaAt(1);
    final before = _fillEveryTable(schema.rawDatabase, 1);
    final db = LibraryDatabase(schema.newConnection());
    await verifier.migrateAndValidate(db, 2);
    final row = (await db.select(db.tracks).get()).single;
    expect(row.artist, before['tracks']!['artist']);
    expect(row.album, before['tracks']!['album']);
    // Null: the library view keys the row on its displayed text, and the
    // scanner (scan version 2) reads the file again on the next scan.
    expect(row.artistKeyText, isNull);
    expect(row.albumKeyText, isNull);
    expect(row.scanVersion, 1);
    await db.close();
  });

  test(
    '2 to 3: tracks keep their rows and get no art until a rescan',
    () async {
      final schema = await verifier.schemaAt(2);
      final before = _fillEveryTable(schema.rawDatabase, 2);
      final db = LibraryDatabase(schema.newConnection());
      await verifier.migrateAndValidate(db, 3);
      final row = (await db.select(db.tracks).get()).single;
      expect(row.title, before['tracks']!['title']);
      expect(row.artistKeyText, isNotNull);
      // Null: the scanner (scan version 3) reads the file again for its
      // picture, and notes the folder's cover file on its next walk.
      expect(row.artOffset, isNull);
      expect(row.artLength, isNull);
      expect(row.artKind, isNull);
      expect(row.folderArtUri, isNull);
      expect(row.folderArtSize, isNull);
      expect(row.folderArtMtime, isNull);
      await db.close();
    },
  );

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
    expect(_userVersion(schema.rawDatabase), _versions.last + 1);
  });

  group('a failed upgrade rolls back as one transaction', () {
    // A database at the current version, opened by a build that claims the
    // next one, with steps that change things and then fail.
    Future<void> expectRolledBack(
      Future<void> Function(GeneratedDatabase db) steps,
    ) async {
      final current = _versions.last;
      final schema = await verifier.schemaAt(current);
      final before = _fillEveryTable(schema.rawDatabase, current);
      String schemaSql() => schema.rawDatabase
          .select('SELECT sql FROM sqlite_schema ORDER BY name')
          .map((r) => r['sql'])
          .join('\n');
      final schemaBefore = schemaSql();

      final db = LibraryDatabase.withStepsForTesting(
        schema.newConnection(),
        schemaVersion: current + 1,
        steps: (m, from, to) => steps(m.database),
      );
      await expectLater(db.customSelect('SELECT 1').get(), throwsA(anything));
      await db.close();

      expect(schemaSql(), schemaBefore);
      expect(_userVersion(schema.rawDatabase), current);
      for (final MapEntry(key: table, value: row) in before.entries) {
        final rows = schema.rawDatabase.select('SELECT * FROM "$table"');
        expect(rows, hasLength(1), reason: table);
        expect(rows.single.values, row.values.toList(), reason: table);
      }
    }

    test('a step that throws part-way', () async {
      await expectRolledBack((db) async {
        await db.customStatement('CREATE TABLE ratings (track_id INTEGER)');
        await db.customStatement('ALTER TABLE tracks ADD COLUMN extra TEXT');
        await db.customStatement('DELETE FROM tracks');
        throw StateError('step 2 failed');
      });
    });

    test('a step that breaks a foreign key', () async {
      await expectRolledBack((db) async {
        await db.customStatement('DELETE FROM sources');
      });
    });
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
/// declared type in every column, and returns what was written. Tables are
/// filled parents first (by their foreign keys), and a foreign key column
/// gets the value of the column it references in the parent's row, so every
/// row is valid with foreign keys on (they are on while filling, and checked
/// after).
Map<String, Map<String, Object?>> _fillEveryTable(sql.Database raw, int v) {
  final names = [
    for (final t in raw.select(
      "SELECT name FROM sqlite_schema WHERE type = 'table' "
      "AND name NOT LIKE 'sqlite_%' ORDER BY name",
    ))
      t['name'] as String,
  ];
  // Per table: child column -> (parent table, parent column).
  final refs = <String, Map<String, (String, String)>>{
    for (final table in names)
      table: {
        for (final fk in raw.select('PRAGMA foreign_key_list("$table")'))
          fk['from'] as String: (fk['table'] as String, fk['to'] as String),
      },
  };
  final order = <String>[];
  void visit(String table, Set<String> path) {
    if (order.contains(table)) return;
    if (!path.add(table)) {
      throw StateError('foreign key cycle through $table: fill it by hand');
    }
    for (final (parent, _) in refs[table]!.values) {
      if (parent != table) visit(parent, path);
    }
    path.remove(table);
    order.add(table);
  }

  for (final table in names) {
    visit(table, {});
  }

  raw.execute('PRAGMA foreign_keys = ON');
  final written = <String, Map<String, Object?>>{};
  for (final table in order) {
    final row = <String, Object?>{};
    for (final c in raw.select('PRAGMA table_info("$table")')) {
      final type = (c['type'] as String).toUpperCase();
      final name = c['name'] as String;
      final ref = refs[table]![name];
      row[name] = ref != null && ref.$1 != table
          ? written[ref.$1]![ref.$2]
          : switch (type) {
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
  expect(raw.select('PRAGMA foreign_key_check'), isEmpty);
  raw.execute('PRAGMA foreign_keys = OFF');
  return written;
}

int _userVersion(sql.Database raw) =>
    raw.select('PRAGMA user_version').single.values.single as int;
