// SPDX-License-Identifier: MPL-2.0

// Migration tests for the ratings and play counts database (decisions 0008
// and 0011, owner decision 20), on drift's generated schema verifier.
// generated/ is written from drift_schemas/stats/ by `dart run drift_dev
// make-migrations` (with one version, by `drift_dev schema generate
// --data-classes --companions`); do not edit it by hand.
//
// Version 1 is the baseline. When version 2 comes, add the library
// database's simple and every-row-kept tests here (test/drift/library/).

import 'package:drift/drift.dart' hide isNull;
import 'package:drift/native.dart';
import 'package:drift_dev/api/migrations_native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/data/db/stats_database.dart';
import 'package:sqlite3/sqlite3.dart' as sql;

import 'generated/schema.dart';

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
      final db = StatsDatabase(NativeDatabase.memory());
      expect(_versions.last, db.schemaVersion);
      await db.close();
    },
  );

  test('a new database matches the snapshot of the current version', () async {
    final db = StatsDatabase(await verifier.startAt(_versions.last));
    await verifier.migrateAndValidate(db, _versions.last);
    await db.close();
  });

  test('the table refuses values the firmware cannot hold', () async {
    final db = StatsDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    await db
        .into(db.trackStats)
        .insert(TrackStatsCompanion.insert(path: '/a.flac', size: 1));
    final row = (await db.select(db.trackStats).get()).single;
    expect((row.rating, row.plays, row.lastPlayed, row.stamp), (0, 0, 0, 0));
    for (final bad in [
      'UPDATE track_stats SET rating = 6',
      'UPDATE track_stats SET rating = -1',
      'UPDATE track_stats SET plays = 65536',
    ]) {
      await expectLater(db.customStatement(bad), throwsA(anything));
    }
  });

  test('a database from a newer build is refused, not dropped', () async {
    final schema = await verifier.schemaAt(_versions.last);
    schema.rawDatabase.execute(
      "INSERT INTO track_stats (path, size, rating) VALUES ('/a.flac', 1, 4)",
    );
    schema.rawDatabase.execute('PRAGMA user_version = ${_versions.last + 1}');

    final db = StatsDatabase(schema.newConnection());
    await expectLater(db.customSelect('SELECT 1').get(), throwsA(anything));
    await db.close();
    expect(
      schema.rawDatabase.select('SELECT rating FROM track_stats').single.values,
      [4],
    );
    expect(_userVersion(schema.rawDatabase), _versions.last + 1);
  });

  test('an upgrade with no step fails and changes nothing', () async {
    final schema = await verifier.schemaAt(_versions.last);
    schema.rawDatabase.execute(
      "INSERT INTO track_stats (path, size, plays) VALUES ('/a.flac', 1, 3)",
    );
    final db = StatsDatabase.withVersionForTesting(
      schema.newConnection(),
      schemaVersion: _versions.last + 1,
    );
    await expectLater(db.customSelect('SELECT 1').get(), throwsA(anything));
    await db.close();
    expect(
      schema.rawDatabase.select('SELECT plays FROM track_stats').single.values,
      [3],
    );
    expect(_userVersion(schema.rawDatabase), _versions.last);
  });
}

int _userVersion(sql.Database raw) =>
    raw.select('PRAGMA user_version').single.values.single as int;
