// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.steps.dart';

part 'library_database.g.dart';

/// Where tracks come from (decision 0005). R1 writes only `local_folder`.
@DataClassName('SourceRow')
class Sources extends Table {
  IntColumn get id => integer().autoIncrement()();

  /// `local_folder`, later `nas` and `device`.
  TextColumn get kind => text()();

  /// The folder id from neupod_storage (tree URI or bookmark id).
  TextColumn get uriOrBookmark => text().unique()();
  TextColumn get displayName => text()();
}

/// One row per audio file, unique on (source, relative path).
///
/// Phase 1 keeps artist and album as text on the track; phase 3 moves them
/// into `artists` / `albums` / `genres` tables with their own sort keys, and
/// adds `track_key` content fingerprints and `track_stats` (decision 0005).
@DataClassName('TrackRow')
@TableIndex(name: 'tracks_title_sort', columns: {#titleSortKey, #id})
class Tracks extends Table {
  IntColumn get id => integer().autoIncrement()();
  IntColumn get sourceId =>
      integer().references(Sources, #id, onDelete: KeyAction.cascade)();
  TextColumn get relPath => text()();
  TextColumn get uri => text()();
  IntColumn get size => integer()();
  IntColumn get mtime => integer()();
  TextColumn get codec => text()();
  TextColumn get title => text()();

  /// The title sort tag (TSOT / TITLESORT), or ''.
  TextColumn get titleSort => text()();

  /// `songSortKey()`: the firmware's key of the title (or file name) and its
  /// sort tag. `ORDER BY` it gives the device's order, since SQLite compares
  /// BLOBs with memcmp.
  BlobColumn get titleSortKey => blob()();
  TextColumn get artist => text()();
  TextColumn get album => text()();
  IntColumn get trackNo => integer()();
  IntColumn get durationMs => integer()();
  IntColumn get sampleRate => integer()();
  IntColumn get bitDepth => integer()();

  /// `UnreadableReason.code` when the tags could not be read, or null.
  IntColumn get reasonCode => integer().nullable()();

  /// The scanner's `scanVersion` this row was read with; a row from another
  /// version is read again on the next scan.
  IntColumn get scanVersion => integer()();

  @override
  List<Set<Column>> get uniqueKeys => [
    {sourceId, relPath},
  ];
}

@DriftDatabase(tables: [Sources, Tracks])
class LibraryDatabase extends _$LibraryDatabase {
  LibraryDatabase(super.e);

  /// Migrations are stepwise and tested (decision 0008). Version 1 is the
  /// baseline. To change a table: bump this, run
  /// `dart run drift_dev make-migrations` (it writes the snapshot in
  /// drift_schemas/library/, regenerates library_database.steps.dart and the
  /// test helpers in test/drift/library/generated/), then add the step to
  /// [_upgrade] and run `dart run build_runner build`.
  @override
  int get schemaVersion => 1;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    onCreate: (m) => m.createAll(),
    onUpgrade: (m, from, to) async {
      // One step per version, each against its snapshot, so a database at any
      // earlier version reaches this one with its rows kept. Never drop and
      // recreate: the database will hold data a rescan cannot rebuild
      // (ratings, play counts, playlists). A downgrade (a database from a
      // newer build) throws a StateError instead of being changed.
      await _upgrade(m, from, to);
      final broken = await customSelect('PRAGMA foreign_key_check').get();
      if (broken.isNotEmpty) {
        throw StateError(
          'migration $from to $to broke foreign keys: '
          '${broken.map((r) => r.data).toList()}',
        );
      }
    },
    beforeOpen: (details) async {
      await customStatement('PRAGMA foreign_keys = ON');
    },
  );

  /// The steps, kept apart from [migration] as drift advises, so each step
  /// runs against its own snapshot. Add `from1To2: (m, schema) async {...}`
  /// here when version 2 exists.
  OnUpgrade get _upgrade => stepByStep();
}
