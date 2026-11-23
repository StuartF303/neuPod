// SPDX-License-Identifier: MPL-2.0

import 'package:drift/drift.dart';
import 'package:flutter/foundation.dart' show visibleForTesting;
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

  /// The artist and album as displayed: the whole tag, decoded.
  TextColumn get artist => text()();
  TextColumn get album => text()();

  /// The artist tag exactly as the firmware's parser stores it
  /// (`meta_text_to_utf8` into `char artist[64]`: at most 63 bytes, UTF-16
  /// surrogates as `?`, malformed UTF-8 copied raw, empty for a FLAC whose
  /// comment block is over the scanner's scratch), for the artist's key and
  /// the compilation test. Empty when the firmware has no artist tag. Null in
  /// a row written before schema version 2, until a rescan reads the file
  /// again (scan version 2); the library view then uses [artist].
  BlobColumn get artistKeyText => blob().nullable()();

  /// The album tag as the firmware stores it (`char album[64]`), as
  /// [artistKeyText].
  BlobColumn get albumKeyText => blob().nullable()();

  /// The album artist tag as displayed (ALBUMARTIST, TPE2), or ''. Rows
  /// written before schema version 3 have '' and are read again by scan
  /// version 3, as are the columns below.
  TextColumn get albumArtist => text().withDefault(const Constant(''))();

  /// The album artist tag as the firmware stores it (`char
  /// album_artist[64]`), as [artistKeyText]: what the library folds artists
  /// on and tests for "Various Artists". Null before schema version 3.
  BlobColumn get albumArtistKeyText => blob().nullable()();

  /// The artist, album and album artist sort tags as the firmware stores
  /// them (`char sort_artist[32]` and so on, meta.h): the artist and album
  /// entities are keyed on them when present (`index_file`, `ent_fill` in
  /// lib_build.c). Null before schema version 3.
  BlobColumn get artistSortKeyText => blob().nullable()();
  BlobColumn get albumSortKeyText => blob().nullable()();
  BlobColumn get albumArtistSortKeyText => blob().nullable()();

  /// The genre as displayed, ID3v1 references resolved (`meta_genre_text`),
  /// or ''.
  TextColumn get genre => text().withDefault(const Constant(''))();

  /// The genre as the firmware stores it (`char genre[32]`): what the
  /// Genres list folds on. Null before schema version 3.
  BlobColumn get genreKeyText => blob().nullable()();

  /// The firmware's disc number (0 when none) and year (0 when none).
  IntColumn get discNo => integer().withDefault(const Constant(0))();
  IntColumn get year => integer().withDefault(const Constant(0))();

  /// The compilation flag (COMPILATION, TCMP) as the firmware reads it.
  BoolColumn get compilation => boolean().withDefault(const Constant(false))();
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
  LibraryDatabase(super.e) : _testVersion = null, _testSteps = null;

  /// Tests only: a database that claims [schemaVersion] and upgrades with
  /// [steps] in place of the real ones, to check what a failing step leaves.
  @visibleForTesting
  LibraryDatabase.withStepsForTesting(
    super.e, {
    required int schemaVersion,
    required OnUpgrade steps,
  }) : _testVersion = schemaVersion,
       _testSteps = steps;

  final int? _testVersion;
  final OnUpgrade? _testSteps;

  /// Migrations are stepwise and tested (decision 0008). Version 1 is the
  /// baseline. To change a table: bump this, run
  /// `dart run drift_dev make-migrations` (it writes the snapshot in
  /// drift_schemas/library/, regenerates library_database.steps.dart and the
  /// test helpers in test/drift/library/generated/), then add the step to
  /// [_upgrade] and run `dart run build_runner build`.
  @override
  int get schemaVersion => _testVersion ?? 3;

  @override
  MigrationStrategy get migration => MigrationStrategy(
    onCreate: (m) => m.createAll(),
    onUpgrade: (m, from, to) async {
      // One step per version, each against its snapshot, so a database at any
      // earlier version reaches this one with its rows kept. Never drop and
      // recreate: the database will hold data a rescan cannot rebuild
      // (ratings, play counts, playlists). A downgrade (a database from a
      // newer build) throws a StateError instead of being changed.
      //
      // drift does not wrap onUpgrade in a transaction, and writes
      // user_version only after beforeOpen succeeds. So every step and the
      // foreign key check run in one transaction: if any of them throws, the
      // whole upgrade rolls back, user_version is unchanged, and the next
      // launch starts again from the old schema. Foreign keys are turned off
      // before it (the pragma is a no-op inside a transaction), as in drift's
      // documented pattern; beforeOpen turns them back on.
      await customStatement('PRAGMA foreign_keys = OFF');
      await transaction(() async {
        await (_testSteps ?? _upgrade)(m, from, to);
        final broken = await customSelect('PRAGMA foreign_key_check').get();
        if (broken.isNotEmpty) {
          throw StateError(
            'migration $from to $to broke foreign keys: '
            '${broken.map((r) => r.data).toList()}',
          );
        }
      });
    },
    beforeOpen: (details) async {
      await customStatement('PRAGMA foreign_keys = ON');
    },
  );

  /// The steps, kept apart from [migration] as drift advises, so each step
  /// runs against its own snapshot.
  ///
  /// - 1 to 2: `tracks.artist_key_text` and `album_key_text`, the firmware's
  ///   text of the artist and album tags. Existing rows keep every column
  ///   and get null in both; the scanner's version went to 2 with them, so
  ///   the next scan reads every file again and fills them.
  /// - 2 to 3: the library tags of `tracks` (album artist, the sort tags,
  ///   genre, disc, year, compilation flag; phase 3). Existing rows keep
  ///   every column and get the defaults ('' / 0 / false, null for the
  ///   firmware texts), which read as untagged; the scanner's version went to
  ///   3 with them, so the next scan reads every file again and fills them.
  OnUpgrade get _upgrade => stepByStep(
    from1To2: (m, schema) async {
      await m.addColumn(schema.tracks, schema.tracks.artistKeyText);
      await m.addColumn(schema.tracks, schema.tracks.albumKeyText);
    },
    from2To3: (m, schema) async {
      final t = schema.tracks;
      await m.addColumn(t, t.albumArtist);
      await m.addColumn(t, t.albumArtistKeyText);
      await m.addColumn(t, t.artistSortKeyText);
      await m.addColumn(t, t.albumSortKeyText);
      await m.addColumn(t, t.albumArtistSortKeyText);
      await m.addColumn(t, t.genre);
      await m.addColumn(t, t.genreKeyText);
      await m.addColumn(t, t.discNo);
      await m.addColumn(t, t.year);
      await m.addColumn(t, t.compilation);
    },
  );
}
