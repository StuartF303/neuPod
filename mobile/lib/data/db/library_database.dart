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

  /// `collateKey()` of the displayed title: `ORDER BY` it gives the
  /// firmware's order, since SQLite compares BLOBs with memcmp.
  BlobColumn get titleSortKey => blob()();
  TextColumn get artist => text()();
  TextColumn get album => text()();
  IntColumn get trackNo => integer()();
  IntColumn get durationMs => integer()();
  IntColumn get sampleRate => integer()();
  IntColumn get bitDepth => integer()();

  /// Why the tags could not be read, or null.
  TextColumn get unreadableReason => text().nullable()();

  @override
  List<Set<Column>> get uniqueKeys => [
    {sourceId, relPath},
  ];
}

@DriftDatabase(tables: [Sources, Tracks])
class LibraryDatabase extends _$LibraryDatabase {
  LibraryDatabase(super.e);

  @override
  int get schemaVersion => 1;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    beforeOpen: (details) async {
      await customStatement('PRAGMA foreign_keys = ON');
    },
  );
}
