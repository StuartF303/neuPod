// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:drift/drift.dart';
import 'package:drift/native.dart';
import 'package:flutter/foundation.dart' show visibleForTesting;

part 'stats_database.g.dart';

/// Ratings, play counts and "last played" (decision 0011), in the app's own
/// database: never in a music folder or on a card.
///
/// Keyed as the firmware keys them (`ratings_key(path, size)` in
/// `app/settings/ratings_store.c`): by the track's path and its size in
/// bytes. The path is the device's form: `/` and the path relative to the
/// library folder (`'/' + tracks.rel_path`), as a card's root gives it. The
/// firmware keeps a 32-bit hash of the two; the app keeps the two
/// themselves, so no two tracks share a row by a hash collision. A rescan
/// that sees a file moved or retagged carries its row to the new key
/// (owner decision 20, `TrackStatsStore.carry`).
///
/// In its own database, apart from the library, so that phone backups keep
/// it while the library (which a rescan rebuilds) stays out of them (owner
/// decision 20). Nothing ties it to the library's rows: a rescan, a removed
/// folder or a file that has gone never deletes a row, and a folder added
/// back gets its numbers back, as a card does on the device.
///
/// One row holds what the firmware keeps in two stores: the rating
/// (`ratings_store`) and the plays entry (`plays_store`: [plays],
/// [lastPlayed], [stamp]). A row with no rating and [stamp] 0 is not kept.
@DataClassName('TrackStatRow')
class TrackStats extends Table {
  TextColumn get path => text()();
  IntColumn get size => integer()();

  // A column's CHECK names the column itself, as drift documents it.

  /// 0 to 5 stars; 0 is not rated.
  IntColumn get rating => integer()
      .withDefault(const Constant(0))
      // ignore: recursive_getters
      .check(rating.isBetweenValues(0, 5))();

  /// Counted plays; stops at 65535, as the firmware's `uint16_t` does.
  IntColumn get plays => integer()
      .withDefault(const Constant(0))
      // ignore: recursive_getters
      .check(plays.isBetweenValues(0, 65535))();

  /// The serial of the last counted play, 0 = never played. The serial
  /// counts up with every play or note, across every row, so it is the
  /// largest [stamp] (the firmware has no clock on every board:
  /// `plays_store.h`).
  IntColumn get lastPlayed => integer().withDefault(const Constant(0))();

  /// The serial of the last touch (a play, a rating of four stars or more);
  /// 0 = the firmware's plays store would have no entry.
  IntColumn get stamp => integer().withDefault(const Constant(0))();

  @override
  Set<Column> get primaryKey => {path, size};
}

/// The ratings and play counts database, `stats.sqlite` in the directory
/// phone backups keep (neupod_storage `backedUpDataPath`).
///
/// Migrations follow decision 0008, as the library database's do: stepwise,
/// each against its snapshot in `drift_schemas/stats/`, all in one
/// transaction, a downgrade refused. Version 1 is the baseline; there is no
/// step yet, so any upgrade throws (and changes nothing) until version 2
/// brings one through `dart run drift_dev make-migrations`.
@DriftDatabase(tables: [TrackStats])
class StatsDatabase extends _$StatsDatabase {
  StatsDatabase(super.e) : _testVersion = null;

  /// Tests only: a database that claims [schemaVersion].
  @visibleForTesting
  StatsDatabase.withVersionForTesting(super.e, {required int schemaVersion})
    : _testVersion = schemaVersion;

  final int? _testVersion;

  /// `make-migrations` reads this getter without running it and cannot
  /// read `_testVersion ?? n`: write the bare number while it runs.
  @override
  int get schemaVersion => _testVersion ?? 1;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    onCreate: (m) => m.createAll(),
    onUpgrade: (m, from, to) async {
      // As LibraryDatabase: every step and the check in one transaction.
      await customStatement('PRAGMA foreign_keys = OFF');
      await transaction(() async {
        throw StateError(
          'stats database: no migration from $from to $to '
          '(a newer build wrote it, or a step is missing)',
        );
      });
    },
    beforeOpen: (details) async {
      await customStatement('PRAGMA foreign_keys = ON');
    },
  );
}

/// Opens the stats database lazily, on first use, in a background isolate,
/// as `stats.sqlite` in the directory [directory] returns.
StatsDatabase openStatsDatabase(Future<String> Function() directory) =>
    StatsDatabase(
      LazyDatabase(() async {
        final dir = await directory();
        return NativeDatabase.createInBackground(File('$dir/stats.sqlite'));
      }),
    );
