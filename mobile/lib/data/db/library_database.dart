// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart';

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

  /// Schema snapshots are in drift_schemas/library/ (dump a new one with
  /// `dart run drift_dev schema dump lib/data/db/library_database.dart
  /// drift_schemas/library/` before changing a table).
  @override
  int get schemaVersion => 1;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    onCreate: (m) => m.createAll(),
    onUpgrade: (m, from, to) async {
      // Version 1 is the first schema; later versions add their steps here,
      // tested against the snapshots (phase 3 brings version 2). Until then
      // any other version (a downgrade, an unknown file) is a cache to drop:
      // a rescan rebuilds it.
      for (final table in allTables.toList().reversed) {
        await m.deleteTable(table.actualTableName);
      }
      await m.createAll();
    },
    beforeOpen: (details) async {
      await customStatement('PRAGMA foreign_keys = ON');
    },
  );
}
