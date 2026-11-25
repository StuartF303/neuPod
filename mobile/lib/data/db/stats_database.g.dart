// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'stats_database.dart';

// ignore_for_file: type=lint
class $TrackStatsTable extends TrackStats
    with TableInfo<$TrackStatsTable, TrackStatRow> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $TrackStatsTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _pathMeta = const VerificationMeta('path');
  @override
  late final GeneratedColumn<String> path = GeneratedColumn<String>(
    'path',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _sizeMeta = const VerificationMeta('size');
  @override
  late final GeneratedColumn<int> size = GeneratedColumn<int>(
    'size',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _ratingMeta = const VerificationMeta('rating');
  @override
  late final GeneratedColumn<int> rating = GeneratedColumn<int>(
    'rating',
    aliasedName,
    false,
    check: () => ComparableExpr(rating).isBetweenValues(0, 5),
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  static const VerificationMeta _playsMeta = const VerificationMeta('plays');
  @override
  late final GeneratedColumn<int> plays = GeneratedColumn<int>(
    'plays',
    aliasedName,
    false,
    check: () => ComparableExpr(plays).isBetweenValues(0, 65535),
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  static const VerificationMeta _lastPlayedMeta = const VerificationMeta(
    'lastPlayed',
  );
  @override
  late final GeneratedColumn<int> lastPlayed = GeneratedColumn<int>(
    'last_played',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  static const VerificationMeta _stampMeta = const VerificationMeta('stamp');
  @override
  late final GeneratedColumn<int> stamp = GeneratedColumn<int>(
    'stamp',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  @override
  List<GeneratedColumn> get $columns => [
    path,
    size,
    rating,
    plays,
    lastPlayed,
    stamp,
  ];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'track_stats';
  @override
  VerificationContext validateIntegrity(
    Insertable<TrackStatRow> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('path')) {
      context.handle(
        _pathMeta,
        path.isAcceptableOrUnknown(data['path']!, _pathMeta),
      );
    } else if (isInserting) {
      context.missing(_pathMeta);
    }
    if (data.containsKey('size')) {
      context.handle(
        _sizeMeta,
        size.isAcceptableOrUnknown(data['size']!, _sizeMeta),
      );
    } else if (isInserting) {
      context.missing(_sizeMeta);
    }
    if (data.containsKey('rating')) {
      context.handle(
        _ratingMeta,
        rating.isAcceptableOrUnknown(data['rating']!, _ratingMeta),
      );
    }
    if (data.containsKey('plays')) {
      context.handle(
        _playsMeta,
        plays.isAcceptableOrUnknown(data['plays']!, _playsMeta),
      );
    }
    if (data.containsKey('last_played')) {
      context.handle(
        _lastPlayedMeta,
        lastPlayed.isAcceptableOrUnknown(data['last_played']!, _lastPlayedMeta),
      );
    }
    if (data.containsKey('stamp')) {
      context.handle(
        _stampMeta,
        stamp.isAcceptableOrUnknown(data['stamp']!, _stampMeta),
      );
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {path, size};
  @override
  TrackStatRow map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return TrackStatRow(
      path: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}path'],
      )!,
      size: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}size'],
      )!,
      rating: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}rating'],
      )!,
      plays: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}plays'],
      )!,
      lastPlayed: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}last_played'],
      )!,
      stamp: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}stamp'],
      )!,
    );
  }

  @override
  $TrackStatsTable createAlias(String alias) {
    return $TrackStatsTable(attachedDatabase, alias);
  }
}

class TrackStatRow extends DataClass implements Insertable<TrackStatRow> {
  final String path;
  final int size;

  /// 0 to 5 stars; 0 is not rated.
  final int rating;

  /// Counted plays; stops at 65535, as the firmware's `uint16_t` does.
  final int plays;

  /// The serial of the last counted play, 0 = never played. The serial
  /// counts up with every play or note, across every row, so it is the
  /// largest [stamp] (the firmware has no clock on every board:
  /// `plays_store.h`).
  final int lastPlayed;

  /// The serial of the last touch (a play, a rating of four stars or more);
  /// 0 = the firmware's plays store would have no entry.
  final int stamp;
  const TrackStatRow({
    required this.path,
    required this.size,
    required this.rating,
    required this.plays,
    required this.lastPlayed,
    required this.stamp,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['path'] = Variable<String>(path);
    map['size'] = Variable<int>(size);
    map['rating'] = Variable<int>(rating);
    map['plays'] = Variable<int>(plays);
    map['last_played'] = Variable<int>(lastPlayed);
    map['stamp'] = Variable<int>(stamp);
    return map;
  }

  TrackStatsCompanion toCompanion(bool nullToAbsent) {
    return TrackStatsCompanion(
      path: Value(path),
      size: Value(size),
      rating: Value(rating),
      plays: Value(plays),
      lastPlayed: Value(lastPlayed),
      stamp: Value(stamp),
    );
  }

  factory TrackStatRow.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return TrackStatRow(
      path: serializer.fromJson<String>(json['path']),
      size: serializer.fromJson<int>(json['size']),
      rating: serializer.fromJson<int>(json['rating']),
      plays: serializer.fromJson<int>(json['plays']),
      lastPlayed: serializer.fromJson<int>(json['lastPlayed']),
      stamp: serializer.fromJson<int>(json['stamp']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'path': serializer.toJson<String>(path),
      'size': serializer.toJson<int>(size),
      'rating': serializer.toJson<int>(rating),
      'plays': serializer.toJson<int>(plays),
      'lastPlayed': serializer.toJson<int>(lastPlayed),
      'stamp': serializer.toJson<int>(stamp),
    };
  }

  TrackStatRow copyWith({
    String? path,
    int? size,
    int? rating,
    int? plays,
    int? lastPlayed,
    int? stamp,
  }) => TrackStatRow(
    path: path ?? this.path,
    size: size ?? this.size,
    rating: rating ?? this.rating,
    plays: plays ?? this.plays,
    lastPlayed: lastPlayed ?? this.lastPlayed,
    stamp: stamp ?? this.stamp,
  );
  TrackStatRow copyWithCompanion(TrackStatsCompanion data) {
    return TrackStatRow(
      path: data.path.present ? data.path.value : this.path,
      size: data.size.present ? data.size.value : this.size,
      rating: data.rating.present ? data.rating.value : this.rating,
      plays: data.plays.present ? data.plays.value : this.plays,
      lastPlayed: data.lastPlayed.present
          ? data.lastPlayed.value
          : this.lastPlayed,
      stamp: data.stamp.present ? data.stamp.value : this.stamp,
    );
  }

  @override
  String toString() {
    return (StringBuffer('TrackStatRow(')
          ..write('path: $path, ')
          ..write('size: $size, ')
          ..write('rating: $rating, ')
          ..write('plays: $plays, ')
          ..write('lastPlayed: $lastPlayed, ')
          ..write('stamp: $stamp')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode => Object.hash(path, size, rating, plays, lastPlayed, stamp);
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is TrackStatRow &&
          other.path == this.path &&
          other.size == this.size &&
          other.rating == this.rating &&
          other.plays == this.plays &&
          other.lastPlayed == this.lastPlayed &&
          other.stamp == this.stamp);
}

class TrackStatsCompanion extends UpdateCompanion<TrackStatRow> {
  final Value<String> path;
  final Value<int> size;
  final Value<int> rating;
  final Value<int> plays;
  final Value<int> lastPlayed;
  final Value<int> stamp;
  final Value<int> rowid;
  const TrackStatsCompanion({
    this.path = const Value.absent(),
    this.size = const Value.absent(),
    this.rating = const Value.absent(),
    this.plays = const Value.absent(),
    this.lastPlayed = const Value.absent(),
    this.stamp = const Value.absent(),
    this.rowid = const Value.absent(),
  });
  TrackStatsCompanion.insert({
    required String path,
    required int size,
    this.rating = const Value.absent(),
    this.plays = const Value.absent(),
    this.lastPlayed = const Value.absent(),
    this.stamp = const Value.absent(),
    this.rowid = const Value.absent(),
  }) : path = Value(path),
       size = Value(size);
  static Insertable<TrackStatRow> custom({
    Expression<String>? path,
    Expression<int>? size,
    Expression<int>? rating,
    Expression<int>? plays,
    Expression<int>? lastPlayed,
    Expression<int>? stamp,
    Expression<int>? rowid,
  }) {
    return RawValuesInsertable({
      if (path != null) 'path': path,
      if (size != null) 'size': size,
      if (rating != null) 'rating': rating,
      if (plays != null) 'plays': plays,
      if (lastPlayed != null) 'last_played': lastPlayed,
      if (stamp != null) 'stamp': stamp,
      if (rowid != null) 'rowid': rowid,
    });
  }

  TrackStatsCompanion copyWith({
    Value<String>? path,
    Value<int>? size,
    Value<int>? rating,
    Value<int>? plays,
    Value<int>? lastPlayed,
    Value<int>? stamp,
    Value<int>? rowid,
  }) {
    return TrackStatsCompanion(
      path: path ?? this.path,
      size: size ?? this.size,
      rating: rating ?? this.rating,
      plays: plays ?? this.plays,
      lastPlayed: lastPlayed ?? this.lastPlayed,
      stamp: stamp ?? this.stamp,
      rowid: rowid ?? this.rowid,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (path.present) {
      map['path'] = Variable<String>(path.value);
    }
    if (size.present) {
      map['size'] = Variable<int>(size.value);
    }
    if (rating.present) {
      map['rating'] = Variable<int>(rating.value);
    }
    if (plays.present) {
      map['plays'] = Variable<int>(plays.value);
    }
    if (lastPlayed.present) {
      map['last_played'] = Variable<int>(lastPlayed.value);
    }
    if (stamp.present) {
      map['stamp'] = Variable<int>(stamp.value);
    }
    if (rowid.present) {
      map['rowid'] = Variable<int>(rowid.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('TrackStatsCompanion(')
          ..write('path: $path, ')
          ..write('size: $size, ')
          ..write('rating: $rating, ')
          ..write('plays: $plays, ')
          ..write('lastPlayed: $lastPlayed, ')
          ..write('stamp: $stamp, ')
          ..write('rowid: $rowid')
          ..write(')'))
        .toString();
  }
}

abstract class _$StatsDatabase extends GeneratedDatabase {
  _$StatsDatabase(QueryExecutor e) : super(e);
  $StatsDatabaseManager get managers => $StatsDatabaseManager(this);
  late final $TrackStatsTable trackStats = $TrackStatsTable(this);
  @override
  Iterable<TableInfo<Table, Object?>> get allTables =>
      allSchemaEntities.whereType<TableInfo<Table, Object?>>();
  @override
  List<DatabaseSchemaEntity> get allSchemaEntities => [trackStats];
}

typedef $$TrackStatsTableCreateCompanionBuilder = TrackStatsCompanion Function({
  required String path,
  required int size,
  Value<int> rating,
  Value<int> plays,
  Value<int> lastPlayed,
  Value<int> stamp,
  Value<int> rowid,
});
typedef $$TrackStatsTableUpdateCompanionBuilder = TrackStatsCompanion Function({
  Value<String> path,
  Value<int> size,
  Value<int> rating,
  Value<int> plays,
  Value<int> lastPlayed,
  Value<int> stamp,
  Value<int> rowid,
});

class $$TrackStatsTableFilterComposer
    extends Composer<_$StatsDatabase, $TrackStatsTable> {
  $$TrackStatsTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<String> get path => $composableBuilder(
    column: $table.path,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get size => $composableBuilder(
    column: $table.size,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get rating => $composableBuilder(
    column: $table.rating,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get plays => $composableBuilder(
    column: $table.plays,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get lastPlayed => $composableBuilder(
    column: $table.lastPlayed,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get stamp => $composableBuilder(
    column: $table.stamp,
    builder: (column) => ColumnFilters(column),
  );
}

class $$TrackStatsTableOrderingComposer
    extends Composer<_$StatsDatabase, $TrackStatsTable> {
  $$TrackStatsTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<String> get path => $composableBuilder(
    column: $table.path,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get size => $composableBuilder(
    column: $table.size,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get rating => $composableBuilder(
    column: $table.rating,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get plays => $composableBuilder(
    column: $table.plays,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get lastPlayed => $composableBuilder(
    column: $table.lastPlayed,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get stamp => $composableBuilder(
    column: $table.stamp,
    builder: (column) => ColumnOrderings(column),
  );
}

class $$TrackStatsTableAnnotationComposer
    extends Composer<_$StatsDatabase, $TrackStatsTable> {
  $$TrackStatsTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<String> get path =>
      $composableBuilder(column: $table.path, builder: (column) => column);

  GeneratedColumn<int> get size =>
      $composableBuilder(column: $table.size, builder: (column) => column);

  GeneratedColumn<int> get rating =>
      $composableBuilder(column: $table.rating, builder: (column) => column);

  GeneratedColumn<int> get plays =>
      $composableBuilder(column: $table.plays, builder: (column) => column);

  GeneratedColumn<int> get lastPlayed => $composableBuilder(
    column: $table.lastPlayed,
    builder: (column) => column,
  );

  GeneratedColumn<int> get stamp =>
      $composableBuilder(column: $table.stamp, builder: (column) => column);
}

class $$TrackStatsTableTableManager
    extends
        RootTableManager<
          _$StatsDatabase,
          $TrackStatsTable,
          TrackStatRow,
          $$TrackStatsTableFilterComposer,
          $$TrackStatsTableOrderingComposer,
          $$TrackStatsTableAnnotationComposer,
          $$TrackStatsTableCreateCompanionBuilder,
          $$TrackStatsTableUpdateCompanionBuilder,
          (
            TrackStatRow,
            BaseReferences<_$StatsDatabase, $TrackStatsTable, TrackStatRow>,
          ),
          TrackStatRow,
          PrefetchHooks Function()
        > {
  $$TrackStatsTableTableManager(_$StatsDatabase db, $TrackStatsTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$TrackStatsTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$TrackStatsTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$TrackStatsTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<String> path = const Value.absent(),
                Value<int> size = const Value.absent(),
                Value<int> rating = const Value.absent(),
                Value<int> plays = const Value.absent(),
                Value<int> lastPlayed = const Value.absent(),
                Value<int> stamp = const Value.absent(),
                Value<int> rowid = const Value.absent(),
              }) => TrackStatsCompanion(
                path: path,
                size: size,
                rating: rating,
                plays: plays,
                lastPlayed: lastPlayed,
                stamp: stamp,
                rowid: rowid,
              ),
          createCompanionCallback:
              ({
                required String path,
                required int size,
                Value<int> rating = const Value.absent(),
                Value<int> plays = const Value.absent(),
                Value<int> lastPlayed = const Value.absent(),
                Value<int> stamp = const Value.absent(),
                Value<int> rowid = const Value.absent(),
              }) => TrackStatsCompanion.insert(
                path: path,
                size: size,
                rating: rating,
                plays: plays,
                lastPlayed: lastPlayed,
                stamp: stamp,
                rowid: rowid,
              ),
          withReferenceMapper: (p0) => p0
              .map(
                (e) => (
                  e.readTable<$TrackStatsTable, TrackStatRow>(table),
                  BaseReferences<
                    _$StatsDatabase,
                    $TrackStatsTable,
                    TrackStatRow
                  >(db, table, e),
                ),
              )
              .toList(),
          prefetchHooksCallback: null,
        ),
      );
}

typedef $$TrackStatsTableProcessedTableManager =
    ProcessedTableManager<
      _$StatsDatabase,
      $TrackStatsTable,
      TrackStatRow,
      $$TrackStatsTableFilterComposer,
      $$TrackStatsTableOrderingComposer,
      $$TrackStatsTableAnnotationComposer,
      $$TrackStatsTableCreateCompanionBuilder,
      $$TrackStatsTableUpdateCompanionBuilder,
      (
        TrackStatRow,
        BaseReferences<_$StatsDatabase, $TrackStatsTable, TrackStatRow>,
      ),
      TrackStatRow,
      PrefetchHooks Function()
    >;

class $StatsDatabaseManager {
  final _$StatsDatabase _db;
  $StatsDatabaseManager(this._db);
  $$TrackStatsTableTableManager get trackStats =>
      $$TrackStatsTableTableManager(_db, _db.trackStats);
}
