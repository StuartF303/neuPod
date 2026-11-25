// GENERATED CODE - DO NOT MODIFY BY HAND

part of 'library_database.dart';

// ignore_for_file: type=lint
class $SourcesTable extends Sources with TableInfo<$SourcesTable, SourceRow> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $SourcesTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _idMeta = const VerificationMeta('id');
  @override
  late final GeneratedColumn<int> id = GeneratedColumn<int>(
    'id',
    aliasedName,
    false,
    hasAutoIncrement: true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'PRIMARY KEY AUTOINCREMENT',
    ),
  );
  static const VerificationMeta _kindMeta = const VerificationMeta('kind');
  @override
  late final GeneratedColumn<String> kind = GeneratedColumn<String>(
    'kind',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _uriOrBookmarkMeta = const VerificationMeta(
    'uriOrBookmark',
  );
  @override
  late final GeneratedColumn<String> uriOrBookmark = GeneratedColumn<String>(
    'uri_or_bookmark',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
    defaultConstraints: GeneratedColumn.constraintIsAlways('UNIQUE'),
  );
  static const VerificationMeta _displayNameMeta = const VerificationMeta(
    'displayName',
  );
  @override
  late final GeneratedColumn<String> displayName = GeneratedColumn<String>(
    'display_name',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  @override
  List<GeneratedColumn> get $columns => [id, kind, uriOrBookmark, displayName];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'sources';
  @override
  VerificationContext validateIntegrity(
    Insertable<SourceRow> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('id')) {
      context.handle(_idMeta, id.isAcceptableOrUnknown(data['id']!, _idMeta));
    }
    if (data.containsKey('kind')) {
      context.handle(
        _kindMeta,
        kind.isAcceptableOrUnknown(data['kind']!, _kindMeta),
      );
    } else if (isInserting) {
      context.missing(_kindMeta);
    }
    if (data.containsKey('uri_or_bookmark')) {
      context.handle(
        _uriOrBookmarkMeta,
        uriOrBookmark.isAcceptableOrUnknown(
          data['uri_or_bookmark']!,
          _uriOrBookmarkMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_uriOrBookmarkMeta);
    }
    if (data.containsKey('display_name')) {
      context.handle(
        _displayNameMeta,
        displayName.isAcceptableOrUnknown(
          data['display_name']!,
          _displayNameMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_displayNameMeta);
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {id};
  @override
  SourceRow map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return SourceRow(
      id: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}id'],
      )!,
      kind: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}kind'],
      )!,
      uriOrBookmark: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}uri_or_bookmark'],
      )!,
      displayName: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}display_name'],
      )!,
    );
  }

  @override
  $SourcesTable createAlias(String alias) {
    return $SourcesTable(attachedDatabase, alias);
  }
}

class SourceRow extends DataClass implements Insertable<SourceRow> {
  final int id;

  /// `local_folder`, later `nas` and `device`.
  final String kind;

  /// The folder id from neupod_storage (tree URI or bookmark id).
  final String uriOrBookmark;
  final String displayName;
  const SourceRow({
    required this.id,
    required this.kind,
    required this.uriOrBookmark,
    required this.displayName,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['id'] = Variable<int>(id);
    map['kind'] = Variable<String>(kind);
    map['uri_or_bookmark'] = Variable<String>(uriOrBookmark);
    map['display_name'] = Variable<String>(displayName);
    return map;
  }

  SourcesCompanion toCompanion(bool nullToAbsent) {
    return SourcesCompanion(
      id: Value(id),
      kind: Value(kind),
      uriOrBookmark: Value(uriOrBookmark),
      displayName: Value(displayName),
    );
  }

  factory SourceRow.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return SourceRow(
      id: serializer.fromJson<int>(json['id']),
      kind: serializer.fromJson<String>(json['kind']),
      uriOrBookmark: serializer.fromJson<String>(json['uriOrBookmark']),
      displayName: serializer.fromJson<String>(json['displayName']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'id': serializer.toJson<int>(id),
      'kind': serializer.toJson<String>(kind),
      'uriOrBookmark': serializer.toJson<String>(uriOrBookmark),
      'displayName': serializer.toJson<String>(displayName),
    };
  }

  SourceRow copyWith({
    int? id,
    String? kind,
    String? uriOrBookmark,
    String? displayName,
  }) => SourceRow(
    id: id ?? this.id,
    kind: kind ?? this.kind,
    uriOrBookmark: uriOrBookmark ?? this.uriOrBookmark,
    displayName: displayName ?? this.displayName,
  );
  SourceRow copyWithCompanion(SourcesCompanion data) {
    return SourceRow(
      id: data.id.present ? data.id.value : this.id,
      kind: data.kind.present ? data.kind.value : this.kind,
      uriOrBookmark: data.uriOrBookmark.present
          ? data.uriOrBookmark.value
          : this.uriOrBookmark,
      displayName: data.displayName.present
          ? data.displayName.value
          : this.displayName,
    );
  }

  @override
  String toString() {
    return (StringBuffer('SourceRow(')
          ..write('id: $id, ')
          ..write('kind: $kind, ')
          ..write('uriOrBookmark: $uriOrBookmark, ')
          ..write('displayName: $displayName')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode => Object.hash(id, kind, uriOrBookmark, displayName);
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is SourceRow &&
          other.id == this.id &&
          other.kind == this.kind &&
          other.uriOrBookmark == this.uriOrBookmark &&
          other.displayName == this.displayName);
}

class SourcesCompanion extends UpdateCompanion<SourceRow> {
  final Value<int> id;
  final Value<String> kind;
  final Value<String> uriOrBookmark;
  final Value<String> displayName;
  const SourcesCompanion({
    this.id = const Value.absent(),
    this.kind = const Value.absent(),
    this.uriOrBookmark = const Value.absent(),
    this.displayName = const Value.absent(),
  });
  SourcesCompanion.insert({
    this.id = const Value.absent(),
    required String kind,
    required String uriOrBookmark,
    required String displayName,
  }) : kind = Value(kind),
       uriOrBookmark = Value(uriOrBookmark),
       displayName = Value(displayName);
  static Insertable<SourceRow> custom({
    Expression<int>? id,
    Expression<String>? kind,
    Expression<String>? uriOrBookmark,
    Expression<String>? displayName,
  }) {
    return RawValuesInsertable({
      if (id != null) 'id': id,
      if (kind != null) 'kind': kind,
      if (uriOrBookmark != null) 'uri_or_bookmark': uriOrBookmark,
      if (displayName != null) 'display_name': displayName,
    });
  }

  SourcesCompanion copyWith({
    Value<int>? id,
    Value<String>? kind,
    Value<String>? uriOrBookmark,
    Value<String>? displayName,
  }) {
    return SourcesCompanion(
      id: id ?? this.id,
      kind: kind ?? this.kind,
      uriOrBookmark: uriOrBookmark ?? this.uriOrBookmark,
      displayName: displayName ?? this.displayName,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (id.present) {
      map['id'] = Variable<int>(id.value);
    }
    if (kind.present) {
      map['kind'] = Variable<String>(kind.value);
    }
    if (uriOrBookmark.present) {
      map['uri_or_bookmark'] = Variable<String>(uriOrBookmark.value);
    }
    if (displayName.present) {
      map['display_name'] = Variable<String>(displayName.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('SourcesCompanion(')
          ..write('id: $id, ')
          ..write('kind: $kind, ')
          ..write('uriOrBookmark: $uriOrBookmark, ')
          ..write('displayName: $displayName')
          ..write(')'))
        .toString();
  }
}

class $TracksTable extends Tracks with TableInfo<$TracksTable, TrackRow> {
  @override
  final GeneratedDatabase attachedDatabase;
  final String? _alias;
  $TracksTable(this.attachedDatabase, [this._alias]);
  static const VerificationMeta _idMeta = const VerificationMeta('id');
  @override
  late final GeneratedColumn<int> id = GeneratedColumn<int>(
    'id',
    aliasedName,
    false,
    hasAutoIncrement: true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'PRIMARY KEY AUTOINCREMENT',
    ),
  );
  static const VerificationMeta _sourceIdMeta = const VerificationMeta(
    'sourceId',
  );
  @override
  late final GeneratedColumn<int> sourceId = GeneratedColumn<int>(
    'source_id',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'REFERENCES sources (id) ON DELETE CASCADE',
    ),
  );
  static const VerificationMeta _relPathMeta = const VerificationMeta(
    'relPath',
  );
  @override
  late final GeneratedColumn<String> relPath = GeneratedColumn<String>(
    'rel_path',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _uriMeta = const VerificationMeta('uri');
  @override
  late final GeneratedColumn<String> uri = GeneratedColumn<String>(
    'uri',
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
  static const VerificationMeta _mtimeMeta = const VerificationMeta('mtime');
  @override
  late final GeneratedColumn<int> mtime = GeneratedColumn<int>(
    'mtime',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _codecMeta = const VerificationMeta('codec');
  @override
  late final GeneratedColumn<String> codec = GeneratedColumn<String>(
    'codec',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _titleMeta = const VerificationMeta('title');
  @override
  late final GeneratedColumn<String> title = GeneratedColumn<String>(
    'title',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _titleSortMeta = const VerificationMeta(
    'titleSort',
  );
  @override
  late final GeneratedColumn<String> titleSort = GeneratedColumn<String>(
    'title_sort',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _titleSortKeyMeta = const VerificationMeta(
    'titleSortKey',
  );
  @override
  late final GeneratedColumn<Uint8List> titleSortKey =
      GeneratedColumn<Uint8List>(
        'title_sort_key',
        aliasedName,
        false,
        type: DriftSqlType.blob,
        requiredDuringInsert: true,
      );
  static const VerificationMeta _artistMeta = const VerificationMeta('artist');
  @override
  late final GeneratedColumn<String> artist = GeneratedColumn<String>(
    'artist',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _albumMeta = const VerificationMeta('album');
  @override
  late final GeneratedColumn<String> album = GeneratedColumn<String>(
    'album',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _artistKeyTextMeta = const VerificationMeta(
    'artistKeyText',
  );
  @override
  late final GeneratedColumn<Uint8List> artistKeyText =
      GeneratedColumn<Uint8List>(
        'artist_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _albumKeyTextMeta = const VerificationMeta(
    'albumKeyText',
  );
  @override
  late final GeneratedColumn<Uint8List> albumKeyText =
      GeneratedColumn<Uint8List>(
        'album_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _albumArtistMeta = const VerificationMeta(
    'albumArtist',
  );
  @override
  late final GeneratedColumn<String> albumArtist = GeneratedColumn<String>(
    'album_artist',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: false,
    defaultValue: const Constant(''),
  );
  static const VerificationMeta _albumArtistKeyTextMeta =
      const VerificationMeta('albumArtistKeyText');
  @override
  late final GeneratedColumn<Uint8List> albumArtistKeyText =
      GeneratedColumn<Uint8List>(
        'album_artist_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _artistSortKeyTextMeta = const VerificationMeta(
    'artistSortKeyText',
  );
  @override
  late final GeneratedColumn<Uint8List> artistSortKeyText =
      GeneratedColumn<Uint8List>(
        'artist_sort_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _albumSortKeyTextMeta = const VerificationMeta(
    'albumSortKeyText',
  );
  @override
  late final GeneratedColumn<Uint8List> albumSortKeyText =
      GeneratedColumn<Uint8List>(
        'album_sort_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _albumArtistSortKeyTextMeta =
      const VerificationMeta('albumArtistSortKeyText');
  @override
  late final GeneratedColumn<Uint8List> albumArtistSortKeyText =
      GeneratedColumn<Uint8List>(
        'album_artist_sort_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _genreMeta = const VerificationMeta('genre');
  @override
  late final GeneratedColumn<String> genre = GeneratedColumn<String>(
    'genre',
    aliasedName,
    false,
    type: DriftSqlType.string,
    requiredDuringInsert: false,
    defaultValue: const Constant(''),
  );
  static const VerificationMeta _genreKeyTextMeta = const VerificationMeta(
    'genreKeyText',
  );
  @override
  late final GeneratedColumn<Uint8List> genreKeyText =
      GeneratedColumn<Uint8List>(
        'genre_key_text',
        aliasedName,
        true,
        type: DriftSqlType.blob,
        requiredDuringInsert: false,
      );
  static const VerificationMeta _discNoMeta = const VerificationMeta('discNo');
  @override
  late final GeneratedColumn<int> discNo = GeneratedColumn<int>(
    'disc_no',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  static const VerificationMeta _yearMeta = const VerificationMeta('year');
  @override
  late final GeneratedColumn<int> year = GeneratedColumn<int>(
    'year',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
    defaultValue: const Constant(0),
  );
  static const VerificationMeta _compilationMeta = const VerificationMeta(
    'compilation',
  );
  @override
  late final GeneratedColumn<bool> compilation = GeneratedColumn<bool>(
    'compilation',
    aliasedName,
    false,
    type: DriftSqlType.bool,
    requiredDuringInsert: false,
    defaultConstraints: GeneratedColumn.constraintIsAlways(
      'CHECK ("compilation" IN (0, 1))',
    ),
    defaultValue: const Constant(false),
  );
  static const VerificationMeta _trackNoMeta = const VerificationMeta(
    'trackNo',
  );
  @override
  late final GeneratedColumn<int> trackNo = GeneratedColumn<int>(
    'track_no',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _durationMsMeta = const VerificationMeta(
    'durationMs',
  );
  @override
  late final GeneratedColumn<int> durationMs = GeneratedColumn<int>(
    'duration_ms',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _sampleRateMeta = const VerificationMeta(
    'sampleRate',
  );
  @override
  late final GeneratedColumn<int> sampleRate = GeneratedColumn<int>(
    'sample_rate',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _bitDepthMeta = const VerificationMeta(
    'bitDepth',
  );
  @override
  late final GeneratedColumn<int> bitDepth = GeneratedColumn<int>(
    'bit_depth',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  static const VerificationMeta _artOffsetMeta = const VerificationMeta(
    'artOffset',
  );
  @override
  late final GeneratedColumn<int> artOffset = GeneratedColumn<int>(
    'art_offset',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _artLengthMeta = const VerificationMeta(
    'artLength',
  );
  @override
  late final GeneratedColumn<int> artLength = GeneratedColumn<int>(
    'art_length',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _artKindMeta = const VerificationMeta(
    'artKind',
  );
  @override
  late final GeneratedColumn<int> artKind = GeneratedColumn<int>(
    'art_kind',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _folderArtUriMeta = const VerificationMeta(
    'folderArtUri',
  );
  @override
  late final GeneratedColumn<String> folderArtUri = GeneratedColumn<String>(
    'folder_art_uri',
    aliasedName,
    true,
    type: DriftSqlType.string,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _folderArtSizeMeta = const VerificationMeta(
    'folderArtSize',
  );
  @override
  late final GeneratedColumn<int> folderArtSize = GeneratedColumn<int>(
    'folder_art_size',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _folderArtMtimeMeta = const VerificationMeta(
    'folderArtMtime',
  );
  @override
  late final GeneratedColumn<int> folderArtMtime = GeneratedColumn<int>(
    'folder_art_mtime',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _audioFingerprintMeta = const VerificationMeta(
    'audioFingerprint',
  );
  @override
  late final GeneratedColumn<String> audioFingerprint = GeneratedColumn<String>(
    'audio_fingerprint',
    aliasedName,
    true,
    type: DriftSqlType.string,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _reasonCodeMeta = const VerificationMeta(
    'reasonCode',
  );
  @override
  late final GeneratedColumn<int> reasonCode = GeneratedColumn<int>(
    'reason_code',
    aliasedName,
    true,
    type: DriftSqlType.int,
    requiredDuringInsert: false,
  );
  static const VerificationMeta _scanVersionMeta = const VerificationMeta(
    'scanVersion',
  );
  @override
  late final GeneratedColumn<int> scanVersion = GeneratedColumn<int>(
    'scan_version',
    aliasedName,
    false,
    type: DriftSqlType.int,
    requiredDuringInsert: true,
  );
  @override
  List<GeneratedColumn> get $columns => [
    id,
    sourceId,
    relPath,
    uri,
    size,
    mtime,
    codec,
    title,
    titleSort,
    titleSortKey,
    artist,
    album,
    artistKeyText,
    albumKeyText,
    albumArtist,
    albumArtistKeyText,
    artistSortKeyText,
    albumSortKeyText,
    albumArtistSortKeyText,
    genre,
    genreKeyText,
    discNo,
    year,
    compilation,
    trackNo,
    durationMs,
    sampleRate,
    bitDepth,
    artOffset,
    artLength,
    artKind,
    folderArtUri,
    folderArtSize,
    folderArtMtime,
    audioFingerprint,
    reasonCode,
    scanVersion,
  ];
  @override
  String get aliasedName => _alias ?? actualTableName;
  @override
  String get actualTableName => $name;
  static const String $name = 'tracks';
  @override
  VerificationContext validateIntegrity(
    Insertable<TrackRow> instance, {
    bool isInserting = false,
  }) {
    final context = VerificationContext();
    final data = instance.toColumns(true);
    if (data.containsKey('id')) {
      context.handle(_idMeta, id.isAcceptableOrUnknown(data['id']!, _idMeta));
    }
    if (data.containsKey('source_id')) {
      context.handle(
        _sourceIdMeta,
        sourceId.isAcceptableOrUnknown(data['source_id']!, _sourceIdMeta),
      );
    } else if (isInserting) {
      context.missing(_sourceIdMeta);
    }
    if (data.containsKey('rel_path')) {
      context.handle(
        _relPathMeta,
        relPath.isAcceptableOrUnknown(data['rel_path']!, _relPathMeta),
      );
    } else if (isInserting) {
      context.missing(_relPathMeta);
    }
    if (data.containsKey('uri')) {
      context.handle(
        _uriMeta,
        uri.isAcceptableOrUnknown(data['uri']!, _uriMeta),
      );
    } else if (isInserting) {
      context.missing(_uriMeta);
    }
    if (data.containsKey('size')) {
      context.handle(
        _sizeMeta,
        size.isAcceptableOrUnknown(data['size']!, _sizeMeta),
      );
    } else if (isInserting) {
      context.missing(_sizeMeta);
    }
    if (data.containsKey('mtime')) {
      context.handle(
        _mtimeMeta,
        mtime.isAcceptableOrUnknown(data['mtime']!, _mtimeMeta),
      );
    } else if (isInserting) {
      context.missing(_mtimeMeta);
    }
    if (data.containsKey('codec')) {
      context.handle(
        _codecMeta,
        codec.isAcceptableOrUnknown(data['codec']!, _codecMeta),
      );
    } else if (isInserting) {
      context.missing(_codecMeta);
    }
    if (data.containsKey('title')) {
      context.handle(
        _titleMeta,
        title.isAcceptableOrUnknown(data['title']!, _titleMeta),
      );
    } else if (isInserting) {
      context.missing(_titleMeta);
    }
    if (data.containsKey('title_sort')) {
      context.handle(
        _titleSortMeta,
        titleSort.isAcceptableOrUnknown(data['title_sort']!, _titleSortMeta),
      );
    } else if (isInserting) {
      context.missing(_titleSortMeta);
    }
    if (data.containsKey('title_sort_key')) {
      context.handle(
        _titleSortKeyMeta,
        titleSortKey.isAcceptableOrUnknown(
          data['title_sort_key']!,
          _titleSortKeyMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_titleSortKeyMeta);
    }
    if (data.containsKey('artist')) {
      context.handle(
        _artistMeta,
        artist.isAcceptableOrUnknown(data['artist']!, _artistMeta),
      );
    } else if (isInserting) {
      context.missing(_artistMeta);
    }
    if (data.containsKey('album')) {
      context.handle(
        _albumMeta,
        album.isAcceptableOrUnknown(data['album']!, _albumMeta),
      );
    } else if (isInserting) {
      context.missing(_albumMeta);
    }
    if (data.containsKey('artist_key_text')) {
      context.handle(
        _artistKeyTextMeta,
        artistKeyText.isAcceptableOrUnknown(
          data['artist_key_text']!,
          _artistKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('album_key_text')) {
      context.handle(
        _albumKeyTextMeta,
        albumKeyText.isAcceptableOrUnknown(
          data['album_key_text']!,
          _albumKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('album_artist')) {
      context.handle(
        _albumArtistMeta,
        albumArtist.isAcceptableOrUnknown(
          data['album_artist']!,
          _albumArtistMeta,
        ),
      );
    }
    if (data.containsKey('album_artist_key_text')) {
      context.handle(
        _albumArtistKeyTextMeta,
        albumArtistKeyText.isAcceptableOrUnknown(
          data['album_artist_key_text']!,
          _albumArtistKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('artist_sort_key_text')) {
      context.handle(
        _artistSortKeyTextMeta,
        artistSortKeyText.isAcceptableOrUnknown(
          data['artist_sort_key_text']!,
          _artistSortKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('album_sort_key_text')) {
      context.handle(
        _albumSortKeyTextMeta,
        albumSortKeyText.isAcceptableOrUnknown(
          data['album_sort_key_text']!,
          _albumSortKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('album_artist_sort_key_text')) {
      context.handle(
        _albumArtistSortKeyTextMeta,
        albumArtistSortKeyText.isAcceptableOrUnknown(
          data['album_artist_sort_key_text']!,
          _albumArtistSortKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('genre')) {
      context.handle(
        _genreMeta,
        genre.isAcceptableOrUnknown(data['genre']!, _genreMeta),
      );
    }
    if (data.containsKey('genre_key_text')) {
      context.handle(
        _genreKeyTextMeta,
        genreKeyText.isAcceptableOrUnknown(
          data['genre_key_text']!,
          _genreKeyTextMeta,
        ),
      );
    }
    if (data.containsKey('disc_no')) {
      context.handle(
        _discNoMeta,
        discNo.isAcceptableOrUnknown(data['disc_no']!, _discNoMeta),
      );
    }
    if (data.containsKey('year')) {
      context.handle(
        _yearMeta,
        year.isAcceptableOrUnknown(data['year']!, _yearMeta),
      );
    }
    if (data.containsKey('compilation')) {
      context.handle(
        _compilationMeta,
        compilation.isAcceptableOrUnknown(
          data['compilation']!,
          _compilationMeta,
        ),
      );
    }
    if (data.containsKey('track_no')) {
      context.handle(
        _trackNoMeta,
        trackNo.isAcceptableOrUnknown(data['track_no']!, _trackNoMeta),
      );
    } else if (isInserting) {
      context.missing(_trackNoMeta);
    }
    if (data.containsKey('duration_ms')) {
      context.handle(
        _durationMsMeta,
        durationMs.isAcceptableOrUnknown(data['duration_ms']!, _durationMsMeta),
      );
    } else if (isInserting) {
      context.missing(_durationMsMeta);
    }
    if (data.containsKey('sample_rate')) {
      context.handle(
        _sampleRateMeta,
        sampleRate.isAcceptableOrUnknown(data['sample_rate']!, _sampleRateMeta),
      );
    } else if (isInserting) {
      context.missing(_sampleRateMeta);
    }
    if (data.containsKey('bit_depth')) {
      context.handle(
        _bitDepthMeta,
        bitDepth.isAcceptableOrUnknown(data['bit_depth']!, _bitDepthMeta),
      );
    } else if (isInserting) {
      context.missing(_bitDepthMeta);
    }
    if (data.containsKey('art_offset')) {
      context.handle(
        _artOffsetMeta,
        artOffset.isAcceptableOrUnknown(data['art_offset']!, _artOffsetMeta),
      );
    }
    if (data.containsKey('art_length')) {
      context.handle(
        _artLengthMeta,
        artLength.isAcceptableOrUnknown(data['art_length']!, _artLengthMeta),
      );
    }
    if (data.containsKey('art_kind')) {
      context.handle(
        _artKindMeta,
        artKind.isAcceptableOrUnknown(data['art_kind']!, _artKindMeta),
      );
    }
    if (data.containsKey('folder_art_uri')) {
      context.handle(
        _folderArtUriMeta,
        folderArtUri.isAcceptableOrUnknown(
          data['folder_art_uri']!,
          _folderArtUriMeta,
        ),
      );
    }
    if (data.containsKey('folder_art_size')) {
      context.handle(
        _folderArtSizeMeta,
        folderArtSize.isAcceptableOrUnknown(
          data['folder_art_size']!,
          _folderArtSizeMeta,
        ),
      );
    }
    if (data.containsKey('folder_art_mtime')) {
      context.handle(
        _folderArtMtimeMeta,
        folderArtMtime.isAcceptableOrUnknown(
          data['folder_art_mtime']!,
          _folderArtMtimeMeta,
        ),
      );
    }
    if (data.containsKey('audio_fingerprint')) {
      context.handle(
        _audioFingerprintMeta,
        audioFingerprint.isAcceptableOrUnknown(
          data['audio_fingerprint']!,
          _audioFingerprintMeta,
        ),
      );
    }
    if (data.containsKey('reason_code')) {
      context.handle(
        _reasonCodeMeta,
        reasonCode.isAcceptableOrUnknown(data['reason_code']!, _reasonCodeMeta),
      );
    }
    if (data.containsKey('scan_version')) {
      context.handle(
        _scanVersionMeta,
        scanVersion.isAcceptableOrUnknown(
          data['scan_version']!,
          _scanVersionMeta,
        ),
      );
    } else if (isInserting) {
      context.missing(_scanVersionMeta);
    }
    return context;
  }

  @override
  Set<GeneratedColumn> get $primaryKey => {id};
  @override
  List<Set<GeneratedColumn>> get uniqueKeys => [
    {sourceId, relPath},
  ];
  @override
  TrackRow map(Map<String, dynamic> data, {String? tablePrefix}) {
    final effectivePrefix = tablePrefix != null ? '$tablePrefix.' : '';
    return TrackRow(
      id: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}id'],
      )!,
      sourceId: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}source_id'],
      )!,
      relPath: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}rel_path'],
      )!,
      uri: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}uri'],
      )!,
      size: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}size'],
      )!,
      mtime: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}mtime'],
      )!,
      codec: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}codec'],
      )!,
      title: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}title'],
      )!,
      titleSort: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}title_sort'],
      )!,
      titleSortKey: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}title_sort_key'],
      )!,
      artist: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}artist'],
      )!,
      album: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}album'],
      )!,
      artistKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}artist_key_text'],
      ),
      albumKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}album_key_text'],
      ),
      albumArtist: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}album_artist'],
      )!,
      albumArtistKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}album_artist_key_text'],
      ),
      artistSortKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}artist_sort_key_text'],
      ),
      albumSortKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}album_sort_key_text'],
      ),
      albumArtistSortKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}album_artist_sort_key_text'],
      ),
      genre: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}genre'],
      )!,
      genreKeyText: attachedDatabase.typeMapping.read(
        DriftSqlType.blob,
        data['${effectivePrefix}genre_key_text'],
      ),
      discNo: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}disc_no'],
      )!,
      year: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}year'],
      )!,
      compilation: attachedDatabase.typeMapping.read(
        DriftSqlType.bool,
        data['${effectivePrefix}compilation'],
      )!,
      trackNo: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}track_no'],
      )!,
      durationMs: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}duration_ms'],
      )!,
      sampleRate: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}sample_rate'],
      )!,
      bitDepth: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}bit_depth'],
      )!,
      artOffset: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}art_offset'],
      ),
      artLength: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}art_length'],
      ),
      artKind: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}art_kind'],
      ),
      folderArtUri: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}folder_art_uri'],
      ),
      folderArtSize: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}folder_art_size'],
      ),
      folderArtMtime: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}folder_art_mtime'],
      ),
      audioFingerprint: attachedDatabase.typeMapping.read(
        DriftSqlType.string,
        data['${effectivePrefix}audio_fingerprint'],
      ),
      reasonCode: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}reason_code'],
      ),
      scanVersion: attachedDatabase.typeMapping.read(
        DriftSqlType.int,
        data['${effectivePrefix}scan_version'],
      )!,
    );
  }

  @override
  $TracksTable createAlias(String alias) {
    return $TracksTable(attachedDatabase, alias);
  }
}

class TrackRow extends DataClass implements Insertable<TrackRow> {
  final int id;
  final int sourceId;
  final String relPath;
  final String uri;
  final int size;
  final int mtime;
  final String codec;
  final String title;

  /// The title sort tag (TSOT / TITLESORT), or ''.
  final String titleSort;

  /// `songSortKey()`: the firmware's key of the title (or file name) and its
  /// sort tag. `ORDER BY` it gives the device's order, since SQLite compares
  /// BLOBs with memcmp.
  final Uint8List titleSortKey;

  /// The artist and album as displayed: the whole tag, decoded.
  final String artist;
  final String album;

  /// The artist tag exactly as the firmware's parser stores it
  /// (`meta_text_to_utf8` into `char artist[64]`: at most 63 bytes, UTF-16
  /// surrogates as `?`, malformed UTF-8 copied raw, empty for a FLAC whose
  /// comment block is over the scanner's scratch), for the artist's key and
  /// the compilation test. Empty when the firmware has no artist tag. Null in
  /// a row written before schema version 2, until a rescan reads the file
  /// again (scan version 2); the library view then uses [artist].
  final Uint8List? artistKeyText;

  /// The album tag as the firmware stores it (`char album[64]`), as
  /// [artistKeyText].
  final Uint8List? albumKeyText;

  /// The album artist tag as displayed (ALBUMARTIST, TPE2), or ''. Rows
  /// written before schema version 3 have '' and are read again by scan
  /// version 3, as are the columns below.
  final String albumArtist;

  /// The album artist tag as the firmware stores it (`char
  /// album_artist[64]`), as [artistKeyText]: what the library folds artists
  /// on and tests for "Various Artists". Null before schema version 3.
  final Uint8List? albumArtistKeyText;

  /// The artist, album and album artist sort tags as the firmware stores
  /// them (`char sort_artist[32]` and so on, meta.h): the artist and album
  /// entities are keyed on them when present (`index_file`, `ent_fill` in
  /// lib_build.c). Null before schema version 3.
  final Uint8List? artistSortKeyText;
  final Uint8List? albumSortKeyText;
  final Uint8List? albumArtistSortKeyText;

  /// The genre as displayed, ID3v1 references resolved (`meta_genre_text`),
  /// or ''.
  final String genre;

  /// The genre as the firmware stores it (`char genre[32]`): what the
  /// Genres list folds on. Null before schema version 3.
  final Uint8List? genreKeyText;

  /// The firmware's disc number (0 when none) and year (0 when none).
  final int discNo;
  final int year;

  /// The compilation flag (COMPILATION, TCMP) as the firmware reads it.
  final bool compilation;
  final int trackNo;
  final int durationMs;
  final int sampleRate;
  final int bitDepth;

  /// The best embedded picture (`art_ref_t`, see `domain/library/art_rules.dart`):
  /// where its bytes are in the file and what they are (`ArtKind.code`). The
  /// bytes stay in the file; the art cache reads them. [artKind] is 0 when the
  /// file has no picture, and all three are null in a row written before
  /// schema version 4, until a rescan reads the file again (scan version 5).
  final int? artOffset;
  final int? artLength;
  final int? artKind;

  /// The cover file in the track's folder that the firmware would take
  /// (`cover.jpg`, `folder.jpg`, `front.jpg`; `art_folder_pick`), by the URI
  /// the folder-access plugin opens, with its size and modification time (so
  /// a changed file makes a new art cache entry). Null when the folder has
  /// none. Set by every scan of the folder, whether or not the track changed.
  final String? folderArtUri;
  final int? folderArtSize;
  final int? folderArtMtime;

  /// The audio's fingerprint (`audioFingerprint` in
  /// `data/scan/audio_fingerprint.dart`): a hash of the first 64 KiB of the
  /// audio payload and its length, with no tag bytes in it. A rescan that
  /// sees a file gone and one new file with the same fingerprint carries its
  /// ratings and play counts to the new path and size (decision 0011). Null
  /// for a file the scanner does not open (Ogg, Opus, raw AAC, not
  /// downloaded), one whose audio could not be found, and every row written
  /// before schema version 5, until a rescan reads the file again (scan
  /// version 6).
  final String? audioFingerprint;

  /// `UnreadableReason.code` when the tags could not be read, or null.
  final int? reasonCode;

  /// The scanner's `scanVersion` this row was read with; a row from another
  /// version is read again on the next scan.
  final int scanVersion;
  const TrackRow({
    required this.id,
    required this.sourceId,
    required this.relPath,
    required this.uri,
    required this.size,
    required this.mtime,
    required this.codec,
    required this.title,
    required this.titleSort,
    required this.titleSortKey,
    required this.artist,
    required this.album,
    this.artistKeyText,
    this.albumKeyText,
    required this.albumArtist,
    this.albumArtistKeyText,
    this.artistSortKeyText,
    this.albumSortKeyText,
    this.albumArtistSortKeyText,
    required this.genre,
    this.genreKeyText,
    required this.discNo,
    required this.year,
    required this.compilation,
    required this.trackNo,
    required this.durationMs,
    required this.sampleRate,
    required this.bitDepth,
    this.artOffset,
    this.artLength,
    this.artKind,
    this.folderArtUri,
    this.folderArtSize,
    this.folderArtMtime,
    this.audioFingerprint,
    this.reasonCode,
    required this.scanVersion,
  });
  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    map['id'] = Variable<int>(id);
    map['source_id'] = Variable<int>(sourceId);
    map['rel_path'] = Variable<String>(relPath);
    map['uri'] = Variable<String>(uri);
    map['size'] = Variable<int>(size);
    map['mtime'] = Variable<int>(mtime);
    map['codec'] = Variable<String>(codec);
    map['title'] = Variable<String>(title);
    map['title_sort'] = Variable<String>(titleSort);
    map['title_sort_key'] = Variable<Uint8List>(titleSortKey);
    map['artist'] = Variable<String>(artist);
    map['album'] = Variable<String>(album);
    if (!nullToAbsent || artistKeyText != null) {
      map['artist_key_text'] = Variable<Uint8List>(artistKeyText);
    }
    if (!nullToAbsent || albumKeyText != null) {
      map['album_key_text'] = Variable<Uint8List>(albumKeyText);
    }
    map['album_artist'] = Variable<String>(albumArtist);
    if (!nullToAbsent || albumArtistKeyText != null) {
      map['album_artist_key_text'] = Variable<Uint8List>(albumArtistKeyText);
    }
    if (!nullToAbsent || artistSortKeyText != null) {
      map['artist_sort_key_text'] = Variable<Uint8List>(artistSortKeyText);
    }
    if (!nullToAbsent || albumSortKeyText != null) {
      map['album_sort_key_text'] = Variable<Uint8List>(albumSortKeyText);
    }
    if (!nullToAbsent || albumArtistSortKeyText != null) {
      map['album_artist_sort_key_text'] = Variable<Uint8List>(
        albumArtistSortKeyText,
      );
    }
    map['genre'] = Variable<String>(genre);
    if (!nullToAbsent || genreKeyText != null) {
      map['genre_key_text'] = Variable<Uint8List>(genreKeyText);
    }
    map['disc_no'] = Variable<int>(discNo);
    map['year'] = Variable<int>(year);
    map['compilation'] = Variable<bool>(compilation);
    map['track_no'] = Variable<int>(trackNo);
    map['duration_ms'] = Variable<int>(durationMs);
    map['sample_rate'] = Variable<int>(sampleRate);
    map['bit_depth'] = Variable<int>(bitDepth);
    if (!nullToAbsent || artOffset != null) {
      map['art_offset'] = Variable<int>(artOffset);
    }
    if (!nullToAbsent || artLength != null) {
      map['art_length'] = Variable<int>(artLength);
    }
    if (!nullToAbsent || artKind != null) {
      map['art_kind'] = Variable<int>(artKind);
    }
    if (!nullToAbsent || folderArtUri != null) {
      map['folder_art_uri'] = Variable<String>(folderArtUri);
    }
    if (!nullToAbsent || folderArtSize != null) {
      map['folder_art_size'] = Variable<int>(folderArtSize);
    }
    if (!nullToAbsent || folderArtMtime != null) {
      map['folder_art_mtime'] = Variable<int>(folderArtMtime);
    }
    if (!nullToAbsent || audioFingerprint != null) {
      map['audio_fingerprint'] = Variable<String>(audioFingerprint);
    }
    if (!nullToAbsent || reasonCode != null) {
      map['reason_code'] = Variable<int>(reasonCode);
    }
    map['scan_version'] = Variable<int>(scanVersion);
    return map;
  }

  TracksCompanion toCompanion(bool nullToAbsent) {
    return TracksCompanion(
      id: Value(id),
      sourceId: Value(sourceId),
      relPath: Value(relPath),
      uri: Value(uri),
      size: Value(size),
      mtime: Value(mtime),
      codec: Value(codec),
      title: Value(title),
      titleSort: Value(titleSort),
      titleSortKey: Value(titleSortKey),
      artist: Value(artist),
      album: Value(album),
      artistKeyText: artistKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(artistKeyText),
      albumKeyText: albumKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(albumKeyText),
      albumArtist: Value(albumArtist),
      albumArtistKeyText: albumArtistKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(albumArtistKeyText),
      artistSortKeyText: artistSortKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(artistSortKeyText),
      albumSortKeyText: albumSortKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(albumSortKeyText),
      albumArtistSortKeyText: albumArtistSortKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(albumArtistSortKeyText),
      genre: Value(genre),
      genreKeyText: genreKeyText == null && nullToAbsent
          ? const Value.absent()
          : Value(genreKeyText),
      discNo: Value(discNo),
      year: Value(year),
      compilation: Value(compilation),
      trackNo: Value(trackNo),
      durationMs: Value(durationMs),
      sampleRate: Value(sampleRate),
      bitDepth: Value(bitDepth),
      artOffset: artOffset == null && nullToAbsent
          ? const Value.absent()
          : Value(artOffset),
      artLength: artLength == null && nullToAbsent
          ? const Value.absent()
          : Value(artLength),
      artKind: artKind == null && nullToAbsent
          ? const Value.absent()
          : Value(artKind),
      folderArtUri: folderArtUri == null && nullToAbsent
          ? const Value.absent()
          : Value(folderArtUri),
      folderArtSize: folderArtSize == null && nullToAbsent
          ? const Value.absent()
          : Value(folderArtSize),
      folderArtMtime: folderArtMtime == null && nullToAbsent
          ? const Value.absent()
          : Value(folderArtMtime),
      audioFingerprint: audioFingerprint == null && nullToAbsent
          ? const Value.absent()
          : Value(audioFingerprint),
      reasonCode: reasonCode == null && nullToAbsent
          ? const Value.absent()
          : Value(reasonCode),
      scanVersion: Value(scanVersion),
    );
  }

  factory TrackRow.fromJson(
    Map<String, dynamic> json, {
    ValueSerializer? serializer,
  }) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return TrackRow(
      id: serializer.fromJson<int>(json['id']),
      sourceId: serializer.fromJson<int>(json['sourceId']),
      relPath: serializer.fromJson<String>(json['relPath']),
      uri: serializer.fromJson<String>(json['uri']),
      size: serializer.fromJson<int>(json['size']),
      mtime: serializer.fromJson<int>(json['mtime']),
      codec: serializer.fromJson<String>(json['codec']),
      title: serializer.fromJson<String>(json['title']),
      titleSort: serializer.fromJson<String>(json['titleSort']),
      titleSortKey: serializer.fromJson<Uint8List>(json['titleSortKey']),
      artist: serializer.fromJson<String>(json['artist']),
      album: serializer.fromJson<String>(json['album']),
      artistKeyText: serializer.fromJson<Uint8List?>(json['artistKeyText']),
      albumKeyText: serializer.fromJson<Uint8List?>(json['albumKeyText']),
      albumArtist: serializer.fromJson<String>(json['albumArtist']),
      albumArtistKeyText: serializer.fromJson<Uint8List?>(
        json['albumArtistKeyText'],
      ),
      artistSortKeyText: serializer.fromJson<Uint8List?>(
        json['artistSortKeyText'],
      ),
      albumSortKeyText: serializer.fromJson<Uint8List?>(
        json['albumSortKeyText'],
      ),
      albumArtistSortKeyText: serializer.fromJson<Uint8List?>(
        json['albumArtistSortKeyText'],
      ),
      genre: serializer.fromJson<String>(json['genre']),
      genreKeyText: serializer.fromJson<Uint8List?>(json['genreKeyText']),
      discNo: serializer.fromJson<int>(json['discNo']),
      year: serializer.fromJson<int>(json['year']),
      compilation: serializer.fromJson<bool>(json['compilation']),
      trackNo: serializer.fromJson<int>(json['trackNo']),
      durationMs: serializer.fromJson<int>(json['durationMs']),
      sampleRate: serializer.fromJson<int>(json['sampleRate']),
      bitDepth: serializer.fromJson<int>(json['bitDepth']),
      artOffset: serializer.fromJson<int?>(json['artOffset']),
      artLength: serializer.fromJson<int?>(json['artLength']),
      artKind: serializer.fromJson<int?>(json['artKind']),
      folderArtUri: serializer.fromJson<String?>(json['folderArtUri']),
      folderArtSize: serializer.fromJson<int?>(json['folderArtSize']),
      folderArtMtime: serializer.fromJson<int?>(json['folderArtMtime']),
      audioFingerprint: serializer.fromJson<String?>(json['audioFingerprint']),
      reasonCode: serializer.fromJson<int?>(json['reasonCode']),
      scanVersion: serializer.fromJson<int>(json['scanVersion']),
    );
  }
  @override
  Map<String, dynamic> toJson({ValueSerializer? serializer}) {
    serializer ??= driftRuntimeOptions.defaultSerializer;
    return <String, dynamic>{
      'id': serializer.toJson<int>(id),
      'sourceId': serializer.toJson<int>(sourceId),
      'relPath': serializer.toJson<String>(relPath),
      'uri': serializer.toJson<String>(uri),
      'size': serializer.toJson<int>(size),
      'mtime': serializer.toJson<int>(mtime),
      'codec': serializer.toJson<String>(codec),
      'title': serializer.toJson<String>(title),
      'titleSort': serializer.toJson<String>(titleSort),
      'titleSortKey': serializer.toJson<Uint8List>(titleSortKey),
      'artist': serializer.toJson<String>(artist),
      'album': serializer.toJson<String>(album),
      'artistKeyText': serializer.toJson<Uint8List?>(artistKeyText),
      'albumKeyText': serializer.toJson<Uint8List?>(albumKeyText),
      'albumArtist': serializer.toJson<String>(albumArtist),
      'albumArtistKeyText': serializer.toJson<Uint8List?>(albumArtistKeyText),
      'artistSortKeyText': serializer.toJson<Uint8List?>(artistSortKeyText),
      'albumSortKeyText': serializer.toJson<Uint8List?>(albumSortKeyText),
      'albumArtistSortKeyText': serializer.toJson<Uint8List?>(
        albumArtistSortKeyText,
      ),
      'genre': serializer.toJson<String>(genre),
      'genreKeyText': serializer.toJson<Uint8List?>(genreKeyText),
      'discNo': serializer.toJson<int>(discNo),
      'year': serializer.toJson<int>(year),
      'compilation': serializer.toJson<bool>(compilation),
      'trackNo': serializer.toJson<int>(trackNo),
      'durationMs': serializer.toJson<int>(durationMs),
      'sampleRate': serializer.toJson<int>(sampleRate),
      'bitDepth': serializer.toJson<int>(bitDepth),
      'artOffset': serializer.toJson<int?>(artOffset),
      'artLength': serializer.toJson<int?>(artLength),
      'artKind': serializer.toJson<int?>(artKind),
      'folderArtUri': serializer.toJson<String?>(folderArtUri),
      'folderArtSize': serializer.toJson<int?>(folderArtSize),
      'folderArtMtime': serializer.toJson<int?>(folderArtMtime),
      'audioFingerprint': serializer.toJson<String?>(audioFingerprint),
      'reasonCode': serializer.toJson<int?>(reasonCode),
      'scanVersion': serializer.toJson<int>(scanVersion),
    };
  }

  TrackRow copyWith({
    int? id,
    int? sourceId,
    String? relPath,
    String? uri,
    int? size,
    int? mtime,
    String? codec,
    String? title,
    String? titleSort,
    Uint8List? titleSortKey,
    String? artist,
    String? album,
    Value<Uint8List?> artistKeyText = const Value.absent(),
    Value<Uint8List?> albumKeyText = const Value.absent(),
    String? albumArtist,
    Value<Uint8List?> albumArtistKeyText = const Value.absent(),
    Value<Uint8List?> artistSortKeyText = const Value.absent(),
    Value<Uint8List?> albumSortKeyText = const Value.absent(),
    Value<Uint8List?> albumArtistSortKeyText = const Value.absent(),
    String? genre,
    Value<Uint8List?> genreKeyText = const Value.absent(),
    int? discNo,
    int? year,
    bool? compilation,
    int? trackNo,
    int? durationMs,
    int? sampleRate,
    int? bitDepth,
    Value<int?> artOffset = const Value.absent(),
    Value<int?> artLength = const Value.absent(),
    Value<int?> artKind = const Value.absent(),
    Value<String?> folderArtUri = const Value.absent(),
    Value<int?> folderArtSize = const Value.absent(),
    Value<int?> folderArtMtime = const Value.absent(),
    Value<String?> audioFingerprint = const Value.absent(),
    Value<int?> reasonCode = const Value.absent(),
    int? scanVersion,
  }) => TrackRow(
    id: id ?? this.id,
    sourceId: sourceId ?? this.sourceId,
    relPath: relPath ?? this.relPath,
    uri: uri ?? this.uri,
    size: size ?? this.size,
    mtime: mtime ?? this.mtime,
    codec: codec ?? this.codec,
    title: title ?? this.title,
    titleSort: titleSort ?? this.titleSort,
    titleSortKey: titleSortKey ?? this.titleSortKey,
    artist: artist ?? this.artist,
    album: album ?? this.album,
    artistKeyText: artistKeyText.present
        ? artistKeyText.value
        : this.artistKeyText,
    albumKeyText: albumKeyText.present ? albumKeyText.value : this.albumKeyText,
    albumArtist: albumArtist ?? this.albumArtist,
    albumArtistKeyText: albumArtistKeyText.present
        ? albumArtistKeyText.value
        : this.albumArtistKeyText,
    artistSortKeyText: artistSortKeyText.present
        ? artistSortKeyText.value
        : this.artistSortKeyText,
    albumSortKeyText: albumSortKeyText.present
        ? albumSortKeyText.value
        : this.albumSortKeyText,
    albumArtistSortKeyText: albumArtistSortKeyText.present
        ? albumArtistSortKeyText.value
        : this.albumArtistSortKeyText,
    genre: genre ?? this.genre,
    genreKeyText: genreKeyText.present ? genreKeyText.value : this.genreKeyText,
    discNo: discNo ?? this.discNo,
    year: year ?? this.year,
    compilation: compilation ?? this.compilation,
    trackNo: trackNo ?? this.trackNo,
    durationMs: durationMs ?? this.durationMs,
    sampleRate: sampleRate ?? this.sampleRate,
    bitDepth: bitDepth ?? this.bitDepth,
    artOffset: artOffset.present ? artOffset.value : this.artOffset,
    artLength: artLength.present ? artLength.value : this.artLength,
    artKind: artKind.present ? artKind.value : this.artKind,
    folderArtUri: folderArtUri.present ? folderArtUri.value : this.folderArtUri,
    folderArtSize: folderArtSize.present
        ? folderArtSize.value
        : this.folderArtSize,
    folderArtMtime: folderArtMtime.present
        ? folderArtMtime.value
        : this.folderArtMtime,
    audioFingerprint: audioFingerprint.present
        ? audioFingerprint.value
        : this.audioFingerprint,
    reasonCode: reasonCode.present ? reasonCode.value : this.reasonCode,
    scanVersion: scanVersion ?? this.scanVersion,
  );
  TrackRow copyWithCompanion(TracksCompanion data) {
    return TrackRow(
      id: data.id.present ? data.id.value : this.id,
      sourceId: data.sourceId.present ? data.sourceId.value : this.sourceId,
      relPath: data.relPath.present ? data.relPath.value : this.relPath,
      uri: data.uri.present ? data.uri.value : this.uri,
      size: data.size.present ? data.size.value : this.size,
      mtime: data.mtime.present ? data.mtime.value : this.mtime,
      codec: data.codec.present ? data.codec.value : this.codec,
      title: data.title.present ? data.title.value : this.title,
      titleSort: data.titleSort.present ? data.titleSort.value : this.titleSort,
      titleSortKey: data.titleSortKey.present
          ? data.titleSortKey.value
          : this.titleSortKey,
      artist: data.artist.present ? data.artist.value : this.artist,
      album: data.album.present ? data.album.value : this.album,
      artistKeyText: data.artistKeyText.present
          ? data.artistKeyText.value
          : this.artistKeyText,
      albumKeyText: data.albumKeyText.present
          ? data.albumKeyText.value
          : this.albumKeyText,
      albumArtist: data.albumArtist.present
          ? data.albumArtist.value
          : this.albumArtist,
      albumArtistKeyText: data.albumArtistKeyText.present
          ? data.albumArtistKeyText.value
          : this.albumArtistKeyText,
      artistSortKeyText: data.artistSortKeyText.present
          ? data.artistSortKeyText.value
          : this.artistSortKeyText,
      albumSortKeyText: data.albumSortKeyText.present
          ? data.albumSortKeyText.value
          : this.albumSortKeyText,
      albumArtistSortKeyText: data.albumArtistSortKeyText.present
          ? data.albumArtistSortKeyText.value
          : this.albumArtistSortKeyText,
      genre: data.genre.present ? data.genre.value : this.genre,
      genreKeyText: data.genreKeyText.present
          ? data.genreKeyText.value
          : this.genreKeyText,
      discNo: data.discNo.present ? data.discNo.value : this.discNo,
      year: data.year.present ? data.year.value : this.year,
      compilation: data.compilation.present
          ? data.compilation.value
          : this.compilation,
      trackNo: data.trackNo.present ? data.trackNo.value : this.trackNo,
      durationMs: data.durationMs.present
          ? data.durationMs.value
          : this.durationMs,
      sampleRate: data.sampleRate.present
          ? data.sampleRate.value
          : this.sampleRate,
      bitDepth: data.bitDepth.present ? data.bitDepth.value : this.bitDepth,
      artOffset: data.artOffset.present ? data.artOffset.value : this.artOffset,
      artLength: data.artLength.present ? data.artLength.value : this.artLength,
      artKind: data.artKind.present ? data.artKind.value : this.artKind,
      folderArtUri: data.folderArtUri.present
          ? data.folderArtUri.value
          : this.folderArtUri,
      folderArtSize: data.folderArtSize.present
          ? data.folderArtSize.value
          : this.folderArtSize,
      folderArtMtime: data.folderArtMtime.present
          ? data.folderArtMtime.value
          : this.folderArtMtime,
      audioFingerprint: data.audioFingerprint.present
          ? data.audioFingerprint.value
          : this.audioFingerprint,
      reasonCode: data.reasonCode.present
          ? data.reasonCode.value
          : this.reasonCode,
      scanVersion: data.scanVersion.present
          ? data.scanVersion.value
          : this.scanVersion,
    );
  }

  @override
  String toString() {
    return (StringBuffer('TrackRow(')
          ..write('id: $id, ')
          ..write('sourceId: $sourceId, ')
          ..write('relPath: $relPath, ')
          ..write('uri: $uri, ')
          ..write('size: $size, ')
          ..write('mtime: $mtime, ')
          ..write('codec: $codec, ')
          ..write('title: $title, ')
          ..write('titleSort: $titleSort, ')
          ..write('titleSortKey: $titleSortKey, ')
          ..write('artist: $artist, ')
          ..write('album: $album, ')
          ..write('artistKeyText: $artistKeyText, ')
          ..write('albumKeyText: $albumKeyText, ')
          ..write('albumArtist: $albumArtist, ')
          ..write('albumArtistKeyText: $albumArtistKeyText, ')
          ..write('artistSortKeyText: $artistSortKeyText, ')
          ..write('albumSortKeyText: $albumSortKeyText, ')
          ..write('albumArtistSortKeyText: $albumArtistSortKeyText, ')
          ..write('genre: $genre, ')
          ..write('genreKeyText: $genreKeyText, ')
          ..write('discNo: $discNo, ')
          ..write('year: $year, ')
          ..write('compilation: $compilation, ')
          ..write('trackNo: $trackNo, ')
          ..write('durationMs: $durationMs, ')
          ..write('sampleRate: $sampleRate, ')
          ..write('bitDepth: $bitDepth, ')
          ..write('artOffset: $artOffset, ')
          ..write('artLength: $artLength, ')
          ..write('artKind: $artKind, ')
          ..write('folderArtUri: $folderArtUri, ')
          ..write('folderArtSize: $folderArtSize, ')
          ..write('folderArtMtime: $folderArtMtime, ')
          ..write('audioFingerprint: $audioFingerprint, ')
          ..write('reasonCode: $reasonCode, ')
          ..write('scanVersion: $scanVersion')
          ..write(')'))
        .toString();
  }

  @override
  int get hashCode => Object.hashAll([
    id,
    sourceId,
    relPath,
    uri,
    size,
    mtime,
    codec,
    title,
    titleSort,
    $driftBlobEquality.hash(titleSortKey),
    artist,
    album,
    $driftBlobEquality.hash(artistKeyText),
    $driftBlobEquality.hash(albumKeyText),
    albumArtist,
    $driftBlobEquality.hash(albumArtistKeyText),
    $driftBlobEquality.hash(artistSortKeyText),
    $driftBlobEquality.hash(albumSortKeyText),
    $driftBlobEquality.hash(albumArtistSortKeyText),
    genre,
    $driftBlobEquality.hash(genreKeyText),
    discNo,
    year,
    compilation,
    trackNo,
    durationMs,
    sampleRate,
    bitDepth,
    artOffset,
    artLength,
    artKind,
    folderArtUri,
    folderArtSize,
    folderArtMtime,
    audioFingerprint,
    reasonCode,
    scanVersion,
  ]);
  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      (other is TrackRow &&
          other.id == this.id &&
          other.sourceId == this.sourceId &&
          other.relPath == this.relPath &&
          other.uri == this.uri &&
          other.size == this.size &&
          other.mtime == this.mtime &&
          other.codec == this.codec &&
          other.title == this.title &&
          other.titleSort == this.titleSort &&
          $driftBlobEquality.equals(other.titleSortKey, this.titleSortKey) &&
          other.artist == this.artist &&
          other.album == this.album &&
          $driftBlobEquality.equals(other.artistKeyText, this.artistKeyText) &&
          $driftBlobEquality.equals(other.albumKeyText, this.albumKeyText) &&
          other.albumArtist == this.albumArtist &&
          $driftBlobEquality.equals(
            other.albumArtistKeyText,
            this.albumArtistKeyText,
          ) &&
          $driftBlobEquality.equals(
            other.artistSortKeyText,
            this.artistSortKeyText,
          ) &&
          $driftBlobEquality.equals(
            other.albumSortKeyText,
            this.albumSortKeyText,
          ) &&
          $driftBlobEquality.equals(
            other.albumArtistSortKeyText,
            this.albumArtistSortKeyText,
          ) &&
          other.genre == this.genre &&
          $driftBlobEquality.equals(other.genreKeyText, this.genreKeyText) &&
          other.discNo == this.discNo &&
          other.year == this.year &&
          other.compilation == this.compilation &&
          other.trackNo == this.trackNo &&
          other.durationMs == this.durationMs &&
          other.sampleRate == this.sampleRate &&
          other.bitDepth == this.bitDepth &&
          other.artOffset == this.artOffset &&
          other.artLength == this.artLength &&
          other.artKind == this.artKind &&
          other.folderArtUri == this.folderArtUri &&
          other.folderArtSize == this.folderArtSize &&
          other.folderArtMtime == this.folderArtMtime &&
          other.audioFingerprint == this.audioFingerprint &&
          other.reasonCode == this.reasonCode &&
          other.scanVersion == this.scanVersion);
}

class TracksCompanion extends UpdateCompanion<TrackRow> {
  final Value<int> id;
  final Value<int> sourceId;
  final Value<String> relPath;
  final Value<String> uri;
  final Value<int> size;
  final Value<int> mtime;
  final Value<String> codec;
  final Value<String> title;
  final Value<String> titleSort;
  final Value<Uint8List> titleSortKey;
  final Value<String> artist;
  final Value<String> album;
  final Value<Uint8List?> artistKeyText;
  final Value<Uint8List?> albumKeyText;
  final Value<String> albumArtist;
  final Value<Uint8List?> albumArtistKeyText;
  final Value<Uint8List?> artistSortKeyText;
  final Value<Uint8List?> albumSortKeyText;
  final Value<Uint8List?> albumArtistSortKeyText;
  final Value<String> genre;
  final Value<Uint8List?> genreKeyText;
  final Value<int> discNo;
  final Value<int> year;
  final Value<bool> compilation;
  final Value<int> trackNo;
  final Value<int> durationMs;
  final Value<int> sampleRate;
  final Value<int> bitDepth;
  final Value<int?> artOffset;
  final Value<int?> artLength;
  final Value<int?> artKind;
  final Value<String?> folderArtUri;
  final Value<int?> folderArtSize;
  final Value<int?> folderArtMtime;
  final Value<String?> audioFingerprint;
  final Value<int?> reasonCode;
  final Value<int> scanVersion;
  const TracksCompanion({
    this.id = const Value.absent(),
    this.sourceId = const Value.absent(),
    this.relPath = const Value.absent(),
    this.uri = const Value.absent(),
    this.size = const Value.absent(),
    this.mtime = const Value.absent(),
    this.codec = const Value.absent(),
    this.title = const Value.absent(),
    this.titleSort = const Value.absent(),
    this.titleSortKey = const Value.absent(),
    this.artist = const Value.absent(),
    this.album = const Value.absent(),
    this.artistKeyText = const Value.absent(),
    this.albumKeyText = const Value.absent(),
    this.albumArtist = const Value.absent(),
    this.albumArtistKeyText = const Value.absent(),
    this.artistSortKeyText = const Value.absent(),
    this.albumSortKeyText = const Value.absent(),
    this.albumArtistSortKeyText = const Value.absent(),
    this.genre = const Value.absent(),
    this.genreKeyText = const Value.absent(),
    this.discNo = const Value.absent(),
    this.year = const Value.absent(),
    this.compilation = const Value.absent(),
    this.trackNo = const Value.absent(),
    this.durationMs = const Value.absent(),
    this.sampleRate = const Value.absent(),
    this.bitDepth = const Value.absent(),
    this.artOffset = const Value.absent(),
    this.artLength = const Value.absent(),
    this.artKind = const Value.absent(),
    this.folderArtUri = const Value.absent(),
    this.folderArtSize = const Value.absent(),
    this.folderArtMtime = const Value.absent(),
    this.audioFingerprint = const Value.absent(),
    this.reasonCode = const Value.absent(),
    this.scanVersion = const Value.absent(),
  });
  TracksCompanion.insert({
    this.id = const Value.absent(),
    required int sourceId,
    required String relPath,
    required String uri,
    required int size,
    required int mtime,
    required String codec,
    required String title,
    required String titleSort,
    required Uint8List titleSortKey,
    required String artist,
    required String album,
    this.artistKeyText = const Value.absent(),
    this.albumKeyText = const Value.absent(),
    this.albumArtist = const Value.absent(),
    this.albumArtistKeyText = const Value.absent(),
    this.artistSortKeyText = const Value.absent(),
    this.albumSortKeyText = const Value.absent(),
    this.albumArtistSortKeyText = const Value.absent(),
    this.genre = const Value.absent(),
    this.genreKeyText = const Value.absent(),
    this.discNo = const Value.absent(),
    this.year = const Value.absent(),
    this.compilation = const Value.absent(),
    required int trackNo,
    required int durationMs,
    required int sampleRate,
    required int bitDepth,
    this.artOffset = const Value.absent(),
    this.artLength = const Value.absent(),
    this.artKind = const Value.absent(),
    this.folderArtUri = const Value.absent(),
    this.folderArtSize = const Value.absent(),
    this.folderArtMtime = const Value.absent(),
    this.audioFingerprint = const Value.absent(),
    this.reasonCode = const Value.absent(),
    required int scanVersion,
  }) : sourceId = Value(sourceId),
       relPath = Value(relPath),
       uri = Value(uri),
       size = Value(size),
       mtime = Value(mtime),
       codec = Value(codec),
       title = Value(title),
       titleSort = Value(titleSort),
       titleSortKey = Value(titleSortKey),
       artist = Value(artist),
       album = Value(album),
       trackNo = Value(trackNo),
       durationMs = Value(durationMs),
       sampleRate = Value(sampleRate),
       bitDepth = Value(bitDepth),
       scanVersion = Value(scanVersion);
  static Insertable<TrackRow> custom({
    Expression<int>? id,
    Expression<int>? sourceId,
    Expression<String>? relPath,
    Expression<String>? uri,
    Expression<int>? size,
    Expression<int>? mtime,
    Expression<String>? codec,
    Expression<String>? title,
    Expression<String>? titleSort,
    Expression<Uint8List>? titleSortKey,
    Expression<String>? artist,
    Expression<String>? album,
    Expression<Uint8List>? artistKeyText,
    Expression<Uint8List>? albumKeyText,
    Expression<String>? albumArtist,
    Expression<Uint8List>? albumArtistKeyText,
    Expression<Uint8List>? artistSortKeyText,
    Expression<Uint8List>? albumSortKeyText,
    Expression<Uint8List>? albumArtistSortKeyText,
    Expression<String>? genre,
    Expression<Uint8List>? genreKeyText,
    Expression<int>? discNo,
    Expression<int>? year,
    Expression<bool>? compilation,
    Expression<int>? trackNo,
    Expression<int>? durationMs,
    Expression<int>? sampleRate,
    Expression<int>? bitDepth,
    Expression<int>? artOffset,
    Expression<int>? artLength,
    Expression<int>? artKind,
    Expression<String>? folderArtUri,
    Expression<int>? folderArtSize,
    Expression<int>? folderArtMtime,
    Expression<String>? audioFingerprint,
    Expression<int>? reasonCode,
    Expression<int>? scanVersion,
  }) {
    return RawValuesInsertable({
      if (id != null) 'id': id,
      if (sourceId != null) 'source_id': sourceId,
      if (relPath != null) 'rel_path': relPath,
      if (uri != null) 'uri': uri,
      if (size != null) 'size': size,
      if (mtime != null) 'mtime': mtime,
      if (codec != null) 'codec': codec,
      if (title != null) 'title': title,
      if (titleSort != null) 'title_sort': titleSort,
      if (titleSortKey != null) 'title_sort_key': titleSortKey,
      if (artist != null) 'artist': artist,
      if (album != null) 'album': album,
      if (artistKeyText != null) 'artist_key_text': artistKeyText,
      if (albumKeyText != null) 'album_key_text': albumKeyText,
      if (albumArtist != null) 'album_artist': albumArtist,
      if (albumArtistKeyText != null)
        'album_artist_key_text': albumArtistKeyText,
      if (artistSortKeyText != null) 'artist_sort_key_text': artistSortKeyText,
      if (albumSortKeyText != null) 'album_sort_key_text': albumSortKeyText,
      if (albumArtistSortKeyText != null)
        'album_artist_sort_key_text': albumArtistSortKeyText,
      if (genre != null) 'genre': genre,
      if (genreKeyText != null) 'genre_key_text': genreKeyText,
      if (discNo != null) 'disc_no': discNo,
      if (year != null) 'year': year,
      if (compilation != null) 'compilation': compilation,
      if (trackNo != null) 'track_no': trackNo,
      if (durationMs != null) 'duration_ms': durationMs,
      if (sampleRate != null) 'sample_rate': sampleRate,
      if (bitDepth != null) 'bit_depth': bitDepth,
      if (artOffset != null) 'art_offset': artOffset,
      if (artLength != null) 'art_length': artLength,
      if (artKind != null) 'art_kind': artKind,
      if (folderArtUri != null) 'folder_art_uri': folderArtUri,
      if (folderArtSize != null) 'folder_art_size': folderArtSize,
      if (folderArtMtime != null) 'folder_art_mtime': folderArtMtime,
      if (audioFingerprint != null) 'audio_fingerprint': audioFingerprint,
      if (reasonCode != null) 'reason_code': reasonCode,
      if (scanVersion != null) 'scan_version': scanVersion,
    });
  }

  TracksCompanion copyWith({
    Value<int>? id,
    Value<int>? sourceId,
    Value<String>? relPath,
    Value<String>? uri,
    Value<int>? size,
    Value<int>? mtime,
    Value<String>? codec,
    Value<String>? title,
    Value<String>? titleSort,
    Value<Uint8List>? titleSortKey,
    Value<String>? artist,
    Value<String>? album,
    Value<Uint8List?>? artistKeyText,
    Value<Uint8List?>? albumKeyText,
    Value<String>? albumArtist,
    Value<Uint8List?>? albumArtistKeyText,
    Value<Uint8List?>? artistSortKeyText,
    Value<Uint8List?>? albumSortKeyText,
    Value<Uint8List?>? albumArtistSortKeyText,
    Value<String>? genre,
    Value<Uint8List?>? genreKeyText,
    Value<int>? discNo,
    Value<int>? year,
    Value<bool>? compilation,
    Value<int>? trackNo,
    Value<int>? durationMs,
    Value<int>? sampleRate,
    Value<int>? bitDepth,
    Value<int?>? artOffset,
    Value<int?>? artLength,
    Value<int?>? artKind,
    Value<String?>? folderArtUri,
    Value<int?>? folderArtSize,
    Value<int?>? folderArtMtime,
    Value<String?>? audioFingerprint,
    Value<int?>? reasonCode,
    Value<int>? scanVersion,
  }) {
    return TracksCompanion(
      id: id ?? this.id,
      sourceId: sourceId ?? this.sourceId,
      relPath: relPath ?? this.relPath,
      uri: uri ?? this.uri,
      size: size ?? this.size,
      mtime: mtime ?? this.mtime,
      codec: codec ?? this.codec,
      title: title ?? this.title,
      titleSort: titleSort ?? this.titleSort,
      titleSortKey: titleSortKey ?? this.titleSortKey,
      artist: artist ?? this.artist,
      album: album ?? this.album,
      artistKeyText: artistKeyText ?? this.artistKeyText,
      albumKeyText: albumKeyText ?? this.albumKeyText,
      albumArtist: albumArtist ?? this.albumArtist,
      albumArtistKeyText: albumArtistKeyText ?? this.albumArtistKeyText,
      artistSortKeyText: artistSortKeyText ?? this.artistSortKeyText,
      albumSortKeyText: albumSortKeyText ?? this.albumSortKeyText,
      albumArtistSortKeyText:
          albumArtistSortKeyText ?? this.albumArtistSortKeyText,
      genre: genre ?? this.genre,
      genreKeyText: genreKeyText ?? this.genreKeyText,
      discNo: discNo ?? this.discNo,
      year: year ?? this.year,
      compilation: compilation ?? this.compilation,
      trackNo: trackNo ?? this.trackNo,
      durationMs: durationMs ?? this.durationMs,
      sampleRate: sampleRate ?? this.sampleRate,
      bitDepth: bitDepth ?? this.bitDepth,
      artOffset: artOffset ?? this.artOffset,
      artLength: artLength ?? this.artLength,
      artKind: artKind ?? this.artKind,
      folderArtUri: folderArtUri ?? this.folderArtUri,
      folderArtSize: folderArtSize ?? this.folderArtSize,
      folderArtMtime: folderArtMtime ?? this.folderArtMtime,
      audioFingerprint: audioFingerprint ?? this.audioFingerprint,
      reasonCode: reasonCode ?? this.reasonCode,
      scanVersion: scanVersion ?? this.scanVersion,
    );
  }

  @override
  Map<String, Expression> toColumns(bool nullToAbsent) {
    final map = <String, Expression>{};
    if (id.present) {
      map['id'] = Variable<int>(id.value);
    }
    if (sourceId.present) {
      map['source_id'] = Variable<int>(sourceId.value);
    }
    if (relPath.present) {
      map['rel_path'] = Variable<String>(relPath.value);
    }
    if (uri.present) {
      map['uri'] = Variable<String>(uri.value);
    }
    if (size.present) {
      map['size'] = Variable<int>(size.value);
    }
    if (mtime.present) {
      map['mtime'] = Variable<int>(mtime.value);
    }
    if (codec.present) {
      map['codec'] = Variable<String>(codec.value);
    }
    if (title.present) {
      map['title'] = Variable<String>(title.value);
    }
    if (titleSort.present) {
      map['title_sort'] = Variable<String>(titleSort.value);
    }
    if (titleSortKey.present) {
      map['title_sort_key'] = Variable<Uint8List>(titleSortKey.value);
    }
    if (artist.present) {
      map['artist'] = Variable<String>(artist.value);
    }
    if (album.present) {
      map['album'] = Variable<String>(album.value);
    }
    if (artistKeyText.present) {
      map['artist_key_text'] = Variable<Uint8List>(artistKeyText.value);
    }
    if (albumKeyText.present) {
      map['album_key_text'] = Variable<Uint8List>(albumKeyText.value);
    }
    if (albumArtist.present) {
      map['album_artist'] = Variable<String>(albumArtist.value);
    }
    if (albumArtistKeyText.present) {
      map['album_artist_key_text'] = Variable<Uint8List>(
        albumArtistKeyText.value,
      );
    }
    if (artistSortKeyText.present) {
      map['artist_sort_key_text'] = Variable<Uint8List>(
        artistSortKeyText.value,
      );
    }
    if (albumSortKeyText.present) {
      map['album_sort_key_text'] = Variable<Uint8List>(albumSortKeyText.value);
    }
    if (albumArtistSortKeyText.present) {
      map['album_artist_sort_key_text'] = Variable<Uint8List>(
        albumArtistSortKeyText.value,
      );
    }
    if (genre.present) {
      map['genre'] = Variable<String>(genre.value);
    }
    if (genreKeyText.present) {
      map['genre_key_text'] = Variable<Uint8List>(genreKeyText.value);
    }
    if (discNo.present) {
      map['disc_no'] = Variable<int>(discNo.value);
    }
    if (year.present) {
      map['year'] = Variable<int>(year.value);
    }
    if (compilation.present) {
      map['compilation'] = Variable<bool>(compilation.value);
    }
    if (trackNo.present) {
      map['track_no'] = Variable<int>(trackNo.value);
    }
    if (durationMs.present) {
      map['duration_ms'] = Variable<int>(durationMs.value);
    }
    if (sampleRate.present) {
      map['sample_rate'] = Variable<int>(sampleRate.value);
    }
    if (bitDepth.present) {
      map['bit_depth'] = Variable<int>(bitDepth.value);
    }
    if (artOffset.present) {
      map['art_offset'] = Variable<int>(artOffset.value);
    }
    if (artLength.present) {
      map['art_length'] = Variable<int>(artLength.value);
    }
    if (artKind.present) {
      map['art_kind'] = Variable<int>(artKind.value);
    }
    if (folderArtUri.present) {
      map['folder_art_uri'] = Variable<String>(folderArtUri.value);
    }
    if (folderArtSize.present) {
      map['folder_art_size'] = Variable<int>(folderArtSize.value);
    }
    if (folderArtMtime.present) {
      map['folder_art_mtime'] = Variable<int>(folderArtMtime.value);
    }
    if (audioFingerprint.present) {
      map['audio_fingerprint'] = Variable<String>(audioFingerprint.value);
    }
    if (reasonCode.present) {
      map['reason_code'] = Variable<int>(reasonCode.value);
    }
    if (scanVersion.present) {
      map['scan_version'] = Variable<int>(scanVersion.value);
    }
    return map;
  }

  @override
  String toString() {
    return (StringBuffer('TracksCompanion(')
          ..write('id: $id, ')
          ..write('sourceId: $sourceId, ')
          ..write('relPath: $relPath, ')
          ..write('uri: $uri, ')
          ..write('size: $size, ')
          ..write('mtime: $mtime, ')
          ..write('codec: $codec, ')
          ..write('title: $title, ')
          ..write('titleSort: $titleSort, ')
          ..write('titleSortKey: $titleSortKey, ')
          ..write('artist: $artist, ')
          ..write('album: $album, ')
          ..write('artistKeyText: $artistKeyText, ')
          ..write('albumKeyText: $albumKeyText, ')
          ..write('albumArtist: $albumArtist, ')
          ..write('albumArtistKeyText: $albumArtistKeyText, ')
          ..write('artistSortKeyText: $artistSortKeyText, ')
          ..write('albumSortKeyText: $albumSortKeyText, ')
          ..write('albumArtistSortKeyText: $albumArtistSortKeyText, ')
          ..write('genre: $genre, ')
          ..write('genreKeyText: $genreKeyText, ')
          ..write('discNo: $discNo, ')
          ..write('year: $year, ')
          ..write('compilation: $compilation, ')
          ..write('trackNo: $trackNo, ')
          ..write('durationMs: $durationMs, ')
          ..write('sampleRate: $sampleRate, ')
          ..write('bitDepth: $bitDepth, ')
          ..write('artOffset: $artOffset, ')
          ..write('artLength: $artLength, ')
          ..write('artKind: $artKind, ')
          ..write('folderArtUri: $folderArtUri, ')
          ..write('folderArtSize: $folderArtSize, ')
          ..write('folderArtMtime: $folderArtMtime, ')
          ..write('audioFingerprint: $audioFingerprint, ')
          ..write('reasonCode: $reasonCode, ')
          ..write('scanVersion: $scanVersion')
          ..write(')'))
        .toString();
  }
}

abstract class _$LibraryDatabase extends GeneratedDatabase {
  _$LibraryDatabase(QueryExecutor e) : super(e);
  $LibraryDatabaseManager get managers => $LibraryDatabaseManager(this);
  late final $SourcesTable sources = $SourcesTable(this);
  late final $TracksTable tracks = $TracksTable(this);
  late final Index tracksTitleSort = Index(
    'tracks_title_sort',
    'CREATE INDEX tracks_title_sort ON tracks (title_sort_key, id)',
  );
  @override
  Iterable<TableInfo<Table, Object?>> get allTables =>
      allSchemaEntities.whereType<TableInfo<Table, Object?>>();
  @override
  List<DatabaseSchemaEntity> get allSchemaEntities => [
    sources,
    tracks,
    tracksTitleSort,
  ];
  @override
  StreamQueryUpdateRules get streamUpdateRules => const StreamQueryUpdateRules([
    WritePropagation(
      on: TableUpdateQuery.onTableName(
        'sources',
        limitUpdateKind: UpdateKind.delete,
      ),
      result: [TableUpdate('tracks', kind: UpdateKind.delete)],
    ),
  ]);
}

typedef $$SourcesTableCreateCompanionBuilder = SourcesCompanion Function({
  Value<int> id,
  required String kind,
  required String uriOrBookmark,
  required String displayName,
});
typedef $$SourcesTableUpdateCompanionBuilder = SourcesCompanion Function({
  Value<int> id,
  Value<String> kind,
  Value<String> uriOrBookmark,
  Value<String> displayName,
});

final class $$SourcesTableReferences
    extends BaseReferences<_$LibraryDatabase, $SourcesTable, SourceRow> {
  $$SourcesTableReferences(super.$_db, super.$_table, super.$_typedResult);

  static MultiTypedResultKey<$TracksTable, List<TrackRow>> _tracksRefsTable(
    _$LibraryDatabase db,
  ) => MultiTypedResultKey.fromTable(
    db.tracks,
    aliasName: 'sources__id__tracks__source_id',
  );

  $$TracksTableProcessedTableManager get tracksRefs {
    final manager = $$TracksTableTableManager(
      $_db,
      $_db.tracks,
    ).filter((f) => f.sourceId.id.sqlEquals($_itemColumn<int>('id')!));

    final cache = $_typedResult.readTableOrNull(_tracksRefsTable($_db));
    return ProcessedTableManager(
      manager.$state.copyWith(prefetchedData: cache),
    );
  }
}

class $$SourcesTableFilterComposer
    extends Composer<_$LibraryDatabase, $SourcesTable> {
  $$SourcesTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get kind => $composableBuilder(
    column: $table.kind,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get uriOrBookmark => $composableBuilder(
    column: $table.uriOrBookmark,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get displayName => $composableBuilder(
    column: $table.displayName,
    builder: (column) => ColumnFilters(column),
  );

  Expression<bool> tracksRefs(
    Expression<bool> Function($$TracksTableFilterComposer f) f,
  ) {
    final $$TracksTableFilterComposer composer = $composerBuilder(
      composer: this,
      getCurrentColumn: (t) => t.id,
      referencedTable: $db.tracks,
      getReferencedColumn: (t) => t.sourceId,
      builder:
          (
            joinBuilder, {
            $addJoinBuilderToRootComposer,
            $removeJoinBuilderFromRootComposer,
          }) => $$TracksTableFilterComposer(
            $db: $db,
            $table: $db.tracks,
            $addJoinBuilderToRootComposer: $addJoinBuilderToRootComposer,
            joinBuilder: joinBuilder,
            $removeJoinBuilderFromRootComposer:
                $removeJoinBuilderFromRootComposer,
          ),
    );
    return f(composer);
  }
}

class $$SourcesTableOrderingComposer
    extends Composer<_$LibraryDatabase, $SourcesTable> {
  $$SourcesTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get kind => $composableBuilder(
    column: $table.kind,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get uriOrBookmark => $composableBuilder(
    column: $table.uriOrBookmark,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get displayName => $composableBuilder(
    column: $table.displayName,
    builder: (column) => ColumnOrderings(column),
  );
}

class $$SourcesTableAnnotationComposer
    extends Composer<_$LibraryDatabase, $SourcesTable> {
  $$SourcesTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<int> get id =>
      $composableBuilder(column: $table.id, builder: (column) => column);

  GeneratedColumn<String> get kind =>
      $composableBuilder(column: $table.kind, builder: (column) => column);

  GeneratedColumn<String> get uriOrBookmark => $composableBuilder(
    column: $table.uriOrBookmark,
    builder: (column) => column,
  );

  GeneratedColumn<String> get displayName => $composableBuilder(
    column: $table.displayName,
    builder: (column) => column,
  );

  Expression<T> tracksRefs<T extends Object>(
    Expression<T> Function($$TracksTableAnnotationComposer a) f,
  ) {
    final $$TracksTableAnnotationComposer composer = $composerBuilder(
      composer: this,
      getCurrentColumn: (t) => t.id,
      referencedTable: $db.tracks,
      getReferencedColumn: (t) => t.sourceId,
      builder:
          (
            joinBuilder, {
            $addJoinBuilderToRootComposer,
            $removeJoinBuilderFromRootComposer,
          }) => $$TracksTableAnnotationComposer(
            $db: $db,
            $table: $db.tracks,
            $addJoinBuilderToRootComposer: $addJoinBuilderToRootComposer,
            joinBuilder: joinBuilder,
            $removeJoinBuilderFromRootComposer:
                $removeJoinBuilderFromRootComposer,
          ),
    );
    return f(composer);
  }
}

class $$SourcesTableTableManager
    extends
        RootTableManager<
          _$LibraryDatabase,
          $SourcesTable,
          SourceRow,
          $$SourcesTableFilterComposer,
          $$SourcesTableOrderingComposer,
          $$SourcesTableAnnotationComposer,
          $$SourcesTableCreateCompanionBuilder,
          $$SourcesTableUpdateCompanionBuilder,
          (SourceRow, $$SourcesTableReferences),
          SourceRow,
          PrefetchHooks Function({bool tracksRefs})
        > {
  $$SourcesTableTableManager(_$LibraryDatabase db, $SourcesTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$SourcesTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$SourcesTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$SourcesTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                Value<String> kind = const Value.absent(),
                Value<String> uriOrBookmark = const Value.absent(),
                Value<String> displayName = const Value.absent(),
              }) => SourcesCompanion(
                id: id,
                kind: kind,
                uriOrBookmark: uriOrBookmark,
                displayName: displayName,
              ),
          createCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                required String kind,
                required String uriOrBookmark,
                required String displayName,
              }) => SourcesCompanion.insert(
                id: id,
                kind: kind,
                uriOrBookmark: uriOrBookmark,
                displayName: displayName,
              ),
          withReferenceMapper: (p0) => p0
              .map(
                (e) => (
                  e.readTable<$SourcesTable, SourceRow>(table),
                  $$SourcesTableReferences(db, table, e),
                ),
              )
              .toList(),
          prefetchHooksCallback: ({tracksRefs = false}) {
            return PrefetchHooks(
              db: db,
              explicitlyWatchedTables: [if (tracksRefs) db.tracks],
              addJoins: null,
              getPrefetchedDataCallback: (items) async {
                return [
                  if (tracksRefs)
                    await $_getPrefetchedDataByColumns<
                      SourceRow,
                      $SourcesTable,
                      TrackRow,
                      $TracksTable
                    >(
                      currentTable: table,
                      referencedTable: $$SourcesTableReferences
                          ._tracksRefsTable(db),
                      managerFromTypedResult: (p0) =>
                          $$SourcesTableReferences(db, table, p0).tracksRefs,
                      currentColumn: 'id',
                      referencedColumn: 'source_id',
                      typedResults: items,
                    ),
                ];
              },
            );
          },
        ),
      );
}

typedef $$SourcesTableProcessedTableManager =
    ProcessedTableManager<
      _$LibraryDatabase,
      $SourcesTable,
      SourceRow,
      $$SourcesTableFilterComposer,
      $$SourcesTableOrderingComposer,
      $$SourcesTableAnnotationComposer,
      $$SourcesTableCreateCompanionBuilder,
      $$SourcesTableUpdateCompanionBuilder,
      (SourceRow, $$SourcesTableReferences),
      SourceRow,
      PrefetchHooks Function({bool tracksRefs})
    >;
typedef $$TracksTableCreateCompanionBuilder = TracksCompanion Function({
  Value<int> id,
  required int sourceId,
  required String relPath,
  required String uri,
  required int size,
  required int mtime,
  required String codec,
  required String title,
  required String titleSort,
  required Uint8List titleSortKey,
  required String artist,
  required String album,
  Value<Uint8List?> artistKeyText,
  Value<Uint8List?> albumKeyText,
  Value<String> albumArtist,
  Value<Uint8List?> albumArtistKeyText,
  Value<Uint8List?> artistSortKeyText,
  Value<Uint8List?> albumSortKeyText,
  Value<Uint8List?> albumArtistSortKeyText,
  Value<String> genre,
  Value<Uint8List?> genreKeyText,
  Value<int> discNo,
  Value<int> year,
  Value<bool> compilation,
  required int trackNo,
  required int durationMs,
  required int sampleRate,
  required int bitDepth,
  Value<int?> artOffset,
  Value<int?> artLength,
  Value<int?> artKind,
  Value<String?> folderArtUri,
  Value<int?> folderArtSize,
  Value<int?> folderArtMtime,
  Value<String?> audioFingerprint,
  Value<int?> reasonCode,
  required int scanVersion,
});
typedef $$TracksTableUpdateCompanionBuilder = TracksCompanion Function({
  Value<int> id,
  Value<int> sourceId,
  Value<String> relPath,
  Value<String> uri,
  Value<int> size,
  Value<int> mtime,
  Value<String> codec,
  Value<String> title,
  Value<String> titleSort,
  Value<Uint8List> titleSortKey,
  Value<String> artist,
  Value<String> album,
  Value<Uint8List?> artistKeyText,
  Value<Uint8List?> albumKeyText,
  Value<String> albumArtist,
  Value<Uint8List?> albumArtistKeyText,
  Value<Uint8List?> artistSortKeyText,
  Value<Uint8List?> albumSortKeyText,
  Value<Uint8List?> albumArtistSortKeyText,
  Value<String> genre,
  Value<Uint8List?> genreKeyText,
  Value<int> discNo,
  Value<int> year,
  Value<bool> compilation,
  Value<int> trackNo,
  Value<int> durationMs,
  Value<int> sampleRate,
  Value<int> bitDepth,
  Value<int?> artOffset,
  Value<int?> artLength,
  Value<int?> artKind,
  Value<String?> folderArtUri,
  Value<int?> folderArtSize,
  Value<int?> folderArtMtime,
  Value<String?> audioFingerprint,
  Value<int?> reasonCode,
  Value<int> scanVersion,
});

final class $$TracksTableReferences
    extends BaseReferences<_$LibraryDatabase, $TracksTable, TrackRow> {
  $$TracksTableReferences(super.$_db, super.$_table, super.$_typedResult);

  static $SourcesTable _sourceIdTable(_$LibraryDatabase db) =>
      db.sources.createAlias('tracks__source_id__sources__id');

  $$SourcesTableProcessedTableManager get sourceId {
    final $_column = $_itemColumn<int>('source_id')!;

    final manager = $$SourcesTableTableManager(
      $_db,
      $_db.sources,
    ).filter((f) => f.id.sqlEquals($_column));
    final item = $_typedResult.readTableOrNull(_sourceIdTable($_db));
    if (item == null) return manager;
    return ProcessedTableManager(
      manager.$state.copyWith(prefetchedData: [item]),
    );
  }
}

class $$TracksTableFilterComposer
    extends Composer<_$LibraryDatabase, $TracksTable> {
  $$TracksTableFilterComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnFilters<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get relPath => $composableBuilder(
    column: $table.relPath,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get uri => $composableBuilder(
    column: $table.uri,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get size => $composableBuilder(
    column: $table.size,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get mtime => $composableBuilder(
    column: $table.mtime,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get codec => $composableBuilder(
    column: $table.codec,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get title => $composableBuilder(
    column: $table.title,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get titleSort => $composableBuilder(
    column: $table.titleSort,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get titleSortKey => $composableBuilder(
    column: $table.titleSortKey,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get artist => $composableBuilder(
    column: $table.artist,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get album => $composableBuilder(
    column: $table.album,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get artistKeyText => $composableBuilder(
    column: $table.artistKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get albumKeyText => $composableBuilder(
    column: $table.albumKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get albumArtist => $composableBuilder(
    column: $table.albumArtist,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get albumArtistKeyText => $composableBuilder(
    column: $table.albumArtistKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get artistSortKeyText => $composableBuilder(
    column: $table.artistSortKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get albumSortKeyText => $composableBuilder(
    column: $table.albumSortKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get albumArtistSortKeyText => $composableBuilder(
    column: $table.albumArtistSortKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get genre => $composableBuilder(
    column: $table.genre,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<Uint8List> get genreKeyText => $composableBuilder(
    column: $table.genreKeyText,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get discNo => $composableBuilder(
    column: $table.discNo,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get year => $composableBuilder(
    column: $table.year,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<bool> get compilation => $composableBuilder(
    column: $table.compilation,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get trackNo => $composableBuilder(
    column: $table.trackNo,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get durationMs => $composableBuilder(
    column: $table.durationMs,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get sampleRate => $composableBuilder(
    column: $table.sampleRate,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get bitDepth => $composableBuilder(
    column: $table.bitDepth,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get artOffset => $composableBuilder(
    column: $table.artOffset,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get artLength => $composableBuilder(
    column: $table.artLength,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get artKind => $composableBuilder(
    column: $table.artKind,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get folderArtUri => $composableBuilder(
    column: $table.folderArtUri,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get folderArtSize => $composableBuilder(
    column: $table.folderArtSize,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get folderArtMtime => $composableBuilder(
    column: $table.folderArtMtime,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<String> get audioFingerprint => $composableBuilder(
    column: $table.audioFingerprint,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get reasonCode => $composableBuilder(
    column: $table.reasonCode,
    builder: (column) => ColumnFilters(column),
  );

  ColumnFilters<int> get scanVersion => $composableBuilder(
    column: $table.scanVersion,
    builder: (column) => ColumnFilters(column),
  );

  $$SourcesTableFilterComposer get sourceId {
    final $$SourcesTableFilterComposer composer = $composerBuilder(
      composer: this,
      getCurrentColumn: (t) => t.sourceId,
      referencedTable: $db.sources,
      getReferencedColumn: (t) => t.id,
      builder:
          (
            joinBuilder, {
            $addJoinBuilderToRootComposer,
            $removeJoinBuilderFromRootComposer,
          }) => $$SourcesTableFilterComposer(
            $db: $db,
            $table: $db.sources,
            $addJoinBuilderToRootComposer: $addJoinBuilderToRootComposer,
            joinBuilder: joinBuilder,
            $removeJoinBuilderFromRootComposer:
                $removeJoinBuilderFromRootComposer,
          ),
    );
    return composer;
  }
}

class $$TracksTableOrderingComposer
    extends Composer<_$LibraryDatabase, $TracksTable> {
  $$TracksTableOrderingComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  ColumnOrderings<int> get id => $composableBuilder(
    column: $table.id,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get relPath => $composableBuilder(
    column: $table.relPath,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get uri => $composableBuilder(
    column: $table.uri,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get size => $composableBuilder(
    column: $table.size,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get mtime => $composableBuilder(
    column: $table.mtime,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get codec => $composableBuilder(
    column: $table.codec,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get title => $composableBuilder(
    column: $table.title,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get titleSort => $composableBuilder(
    column: $table.titleSort,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get titleSortKey => $composableBuilder(
    column: $table.titleSortKey,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get artist => $composableBuilder(
    column: $table.artist,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get album => $composableBuilder(
    column: $table.album,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get artistKeyText => $composableBuilder(
    column: $table.artistKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get albumKeyText => $composableBuilder(
    column: $table.albumKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get albumArtist => $composableBuilder(
    column: $table.albumArtist,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get albumArtistKeyText => $composableBuilder(
    column: $table.albumArtistKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get artistSortKeyText => $composableBuilder(
    column: $table.artistSortKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get albumSortKeyText => $composableBuilder(
    column: $table.albumSortKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get albumArtistSortKeyText => $composableBuilder(
    column: $table.albumArtistSortKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get genre => $composableBuilder(
    column: $table.genre,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<Uint8List> get genreKeyText => $composableBuilder(
    column: $table.genreKeyText,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get discNo => $composableBuilder(
    column: $table.discNo,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get year => $composableBuilder(
    column: $table.year,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<bool> get compilation => $composableBuilder(
    column: $table.compilation,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get trackNo => $composableBuilder(
    column: $table.trackNo,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get durationMs => $composableBuilder(
    column: $table.durationMs,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get sampleRate => $composableBuilder(
    column: $table.sampleRate,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get bitDepth => $composableBuilder(
    column: $table.bitDepth,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get artOffset => $composableBuilder(
    column: $table.artOffset,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get artLength => $composableBuilder(
    column: $table.artLength,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get artKind => $composableBuilder(
    column: $table.artKind,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get folderArtUri => $composableBuilder(
    column: $table.folderArtUri,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get folderArtSize => $composableBuilder(
    column: $table.folderArtSize,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get folderArtMtime => $composableBuilder(
    column: $table.folderArtMtime,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<String> get audioFingerprint => $composableBuilder(
    column: $table.audioFingerprint,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get reasonCode => $composableBuilder(
    column: $table.reasonCode,
    builder: (column) => ColumnOrderings(column),
  );

  ColumnOrderings<int> get scanVersion => $composableBuilder(
    column: $table.scanVersion,
    builder: (column) => ColumnOrderings(column),
  );

  $$SourcesTableOrderingComposer get sourceId {
    final $$SourcesTableOrderingComposer composer = $composerBuilder(
      composer: this,
      getCurrentColumn: (t) => t.sourceId,
      referencedTable: $db.sources,
      getReferencedColumn: (t) => t.id,
      builder:
          (
            joinBuilder, {
            $addJoinBuilderToRootComposer,
            $removeJoinBuilderFromRootComposer,
          }) => $$SourcesTableOrderingComposer(
            $db: $db,
            $table: $db.sources,
            $addJoinBuilderToRootComposer: $addJoinBuilderToRootComposer,
            joinBuilder: joinBuilder,
            $removeJoinBuilderFromRootComposer:
                $removeJoinBuilderFromRootComposer,
          ),
    );
    return composer;
  }
}

class $$TracksTableAnnotationComposer
    extends Composer<_$LibraryDatabase, $TracksTable> {
  $$TracksTableAnnotationComposer({
    required super.$db,
    required super.$table,
    super.joinBuilder,
    super.$addJoinBuilderToRootComposer,
    super.$removeJoinBuilderFromRootComposer,
  });
  GeneratedColumn<int> get id =>
      $composableBuilder(column: $table.id, builder: (column) => column);

  GeneratedColumn<String> get relPath =>
      $composableBuilder(column: $table.relPath, builder: (column) => column);

  GeneratedColumn<String> get uri =>
      $composableBuilder(column: $table.uri, builder: (column) => column);

  GeneratedColumn<int> get size =>
      $composableBuilder(column: $table.size, builder: (column) => column);

  GeneratedColumn<int> get mtime =>
      $composableBuilder(column: $table.mtime, builder: (column) => column);

  GeneratedColumn<String> get codec =>
      $composableBuilder(column: $table.codec, builder: (column) => column);

  GeneratedColumn<String> get title =>
      $composableBuilder(column: $table.title, builder: (column) => column);

  GeneratedColumn<String> get titleSort =>
      $composableBuilder(column: $table.titleSort, builder: (column) => column);

  GeneratedColumn<Uint8List> get titleSortKey => $composableBuilder(
    column: $table.titleSortKey,
    builder: (column) => column,
  );

  GeneratedColumn<String> get artist =>
      $composableBuilder(column: $table.artist, builder: (column) => column);

  GeneratedColumn<String> get album =>
      $composableBuilder(column: $table.album, builder: (column) => column);

  GeneratedColumn<Uint8List> get artistKeyText => $composableBuilder(
    column: $table.artistKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<Uint8List> get albumKeyText => $composableBuilder(
    column: $table.albumKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<String> get albumArtist => $composableBuilder(
    column: $table.albumArtist,
    builder: (column) => column,
  );

  GeneratedColumn<Uint8List> get albumArtistKeyText => $composableBuilder(
    column: $table.albumArtistKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<Uint8List> get artistSortKeyText => $composableBuilder(
    column: $table.artistSortKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<Uint8List> get albumSortKeyText => $composableBuilder(
    column: $table.albumSortKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<Uint8List> get albumArtistSortKeyText => $composableBuilder(
    column: $table.albumArtistSortKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<String> get genre =>
      $composableBuilder(column: $table.genre, builder: (column) => column);

  GeneratedColumn<Uint8List> get genreKeyText => $composableBuilder(
    column: $table.genreKeyText,
    builder: (column) => column,
  );

  GeneratedColumn<int> get discNo =>
      $composableBuilder(column: $table.discNo, builder: (column) => column);

  GeneratedColumn<int> get year =>
      $composableBuilder(column: $table.year, builder: (column) => column);

  GeneratedColumn<bool> get compilation => $composableBuilder(
    column: $table.compilation,
    builder: (column) => column,
  );

  GeneratedColumn<int> get trackNo =>
      $composableBuilder(column: $table.trackNo, builder: (column) => column);

  GeneratedColumn<int> get durationMs => $composableBuilder(
    column: $table.durationMs,
    builder: (column) => column,
  );

  GeneratedColumn<int> get sampleRate => $composableBuilder(
    column: $table.sampleRate,
    builder: (column) => column,
  );

  GeneratedColumn<int> get bitDepth =>
      $composableBuilder(column: $table.bitDepth, builder: (column) => column);

  GeneratedColumn<int> get artOffset =>
      $composableBuilder(column: $table.artOffset, builder: (column) => column);

  GeneratedColumn<int> get artLength =>
      $composableBuilder(column: $table.artLength, builder: (column) => column);

  GeneratedColumn<int> get artKind =>
      $composableBuilder(column: $table.artKind, builder: (column) => column);

  GeneratedColumn<String> get folderArtUri => $composableBuilder(
    column: $table.folderArtUri,
    builder: (column) => column,
  );

  GeneratedColumn<int> get folderArtSize => $composableBuilder(
    column: $table.folderArtSize,
    builder: (column) => column,
  );

  GeneratedColumn<int> get folderArtMtime => $composableBuilder(
    column: $table.folderArtMtime,
    builder: (column) => column,
  );

  GeneratedColumn<String> get audioFingerprint => $composableBuilder(
    column: $table.audioFingerprint,
    builder: (column) => column,
  );

  GeneratedColumn<int> get reasonCode => $composableBuilder(
    column: $table.reasonCode,
    builder: (column) => column,
  );

  GeneratedColumn<int> get scanVersion => $composableBuilder(
    column: $table.scanVersion,
    builder: (column) => column,
  );

  $$SourcesTableAnnotationComposer get sourceId {
    final $$SourcesTableAnnotationComposer composer = $composerBuilder(
      composer: this,
      getCurrentColumn: (t) => t.sourceId,
      referencedTable: $db.sources,
      getReferencedColumn: (t) => t.id,
      builder:
          (
            joinBuilder, {
            $addJoinBuilderToRootComposer,
            $removeJoinBuilderFromRootComposer,
          }) => $$SourcesTableAnnotationComposer(
            $db: $db,
            $table: $db.sources,
            $addJoinBuilderToRootComposer: $addJoinBuilderToRootComposer,
            joinBuilder: joinBuilder,
            $removeJoinBuilderFromRootComposer:
                $removeJoinBuilderFromRootComposer,
          ),
    );
    return composer;
  }
}

class $$TracksTableTableManager
    extends
        RootTableManager<
          _$LibraryDatabase,
          $TracksTable,
          TrackRow,
          $$TracksTableFilterComposer,
          $$TracksTableOrderingComposer,
          $$TracksTableAnnotationComposer,
          $$TracksTableCreateCompanionBuilder,
          $$TracksTableUpdateCompanionBuilder,
          (TrackRow, $$TracksTableReferences),
          TrackRow,
          PrefetchHooks Function({bool sourceId})
        > {
  $$TracksTableTableManager(_$LibraryDatabase db, $TracksTable table)
    : super(
        TableManagerState(
          db: db,
          table: table,
          createFilteringComposer: () =>
              $$TracksTableFilterComposer($db: db, $table: table),
          createOrderingComposer: () =>
              $$TracksTableOrderingComposer($db: db, $table: table),
          createComputedFieldComposer: () =>
              $$TracksTableAnnotationComposer($db: db, $table: table),
          updateCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                Value<int> sourceId = const Value.absent(),
                Value<String> relPath = const Value.absent(),
                Value<String> uri = const Value.absent(),
                Value<int> size = const Value.absent(),
                Value<int> mtime = const Value.absent(),
                Value<String> codec = const Value.absent(),
                Value<String> title = const Value.absent(),
                Value<String> titleSort = const Value.absent(),
                Value<Uint8List> titleSortKey = const Value.absent(),
                Value<String> artist = const Value.absent(),
                Value<String> album = const Value.absent(),
                Value<Uint8List?> artistKeyText = const Value.absent(),
                Value<Uint8List?> albumKeyText = const Value.absent(),
                Value<String> albumArtist = const Value.absent(),
                Value<Uint8List?> albumArtistKeyText = const Value.absent(),
                Value<Uint8List?> artistSortKeyText = const Value.absent(),
                Value<Uint8List?> albumSortKeyText = const Value.absent(),
                Value<Uint8List?> albumArtistSortKeyText = const Value.absent(),
                Value<String> genre = const Value.absent(),
                Value<Uint8List?> genreKeyText = const Value.absent(),
                Value<int> discNo = const Value.absent(),
                Value<int> year = const Value.absent(),
                Value<bool> compilation = const Value.absent(),
                Value<int> trackNo = const Value.absent(),
                Value<int> durationMs = const Value.absent(),
                Value<int> sampleRate = const Value.absent(),
                Value<int> bitDepth = const Value.absent(),
                Value<int?> artOffset = const Value.absent(),
                Value<int?> artLength = const Value.absent(),
                Value<int?> artKind = const Value.absent(),
                Value<String?> folderArtUri = const Value.absent(),
                Value<int?> folderArtSize = const Value.absent(),
                Value<int?> folderArtMtime = const Value.absent(),
                Value<String?> audioFingerprint = const Value.absent(),
                Value<int?> reasonCode = const Value.absent(),
                Value<int> scanVersion = const Value.absent(),
              }) => TracksCompanion(
                id: id,
                sourceId: sourceId,
                relPath: relPath,
                uri: uri,
                size: size,
                mtime: mtime,
                codec: codec,
                title: title,
                titleSort: titleSort,
                titleSortKey: titleSortKey,
                artist: artist,
                album: album,
                artistKeyText: artistKeyText,
                albumKeyText: albumKeyText,
                albumArtist: albumArtist,
                albumArtistKeyText: albumArtistKeyText,
                artistSortKeyText: artistSortKeyText,
                albumSortKeyText: albumSortKeyText,
                albumArtistSortKeyText: albumArtistSortKeyText,
                genre: genre,
                genreKeyText: genreKeyText,
                discNo: discNo,
                year: year,
                compilation: compilation,
                trackNo: trackNo,
                durationMs: durationMs,
                sampleRate: sampleRate,
                bitDepth: bitDepth,
                artOffset: artOffset,
                artLength: artLength,
                artKind: artKind,
                folderArtUri: folderArtUri,
                folderArtSize: folderArtSize,
                folderArtMtime: folderArtMtime,
                audioFingerprint: audioFingerprint,
                reasonCode: reasonCode,
                scanVersion: scanVersion,
              ),
          createCompanionCallback:
              ({
                Value<int> id = const Value.absent(),
                required int sourceId,
                required String relPath,
                required String uri,
                required int size,
                required int mtime,
                required String codec,
                required String title,
                required String titleSort,
                required Uint8List titleSortKey,
                required String artist,
                required String album,
                Value<Uint8List?> artistKeyText = const Value.absent(),
                Value<Uint8List?> albumKeyText = const Value.absent(),
                Value<String> albumArtist = const Value.absent(),
                Value<Uint8List?> albumArtistKeyText = const Value.absent(),
                Value<Uint8List?> artistSortKeyText = const Value.absent(),
                Value<Uint8List?> albumSortKeyText = const Value.absent(),
                Value<Uint8List?> albumArtistSortKeyText = const Value.absent(),
                Value<String> genre = const Value.absent(),
                Value<Uint8List?> genreKeyText = const Value.absent(),
                Value<int> discNo = const Value.absent(),
                Value<int> year = const Value.absent(),
                Value<bool> compilation = const Value.absent(),
                required int trackNo,
                required int durationMs,
                required int sampleRate,
                required int bitDepth,
                Value<int?> artOffset = const Value.absent(),
                Value<int?> artLength = const Value.absent(),
                Value<int?> artKind = const Value.absent(),
                Value<String?> folderArtUri = const Value.absent(),
                Value<int?> folderArtSize = const Value.absent(),
                Value<int?> folderArtMtime = const Value.absent(),
                Value<String?> audioFingerprint = const Value.absent(),
                Value<int?> reasonCode = const Value.absent(),
                required int scanVersion,
              }) => TracksCompanion.insert(
                id: id,
                sourceId: sourceId,
                relPath: relPath,
                uri: uri,
                size: size,
                mtime: mtime,
                codec: codec,
                title: title,
                titleSort: titleSort,
                titleSortKey: titleSortKey,
                artist: artist,
                album: album,
                artistKeyText: artistKeyText,
                albumKeyText: albumKeyText,
                albumArtist: albumArtist,
                albumArtistKeyText: albumArtistKeyText,
                artistSortKeyText: artistSortKeyText,
                albumSortKeyText: albumSortKeyText,
                albumArtistSortKeyText: albumArtistSortKeyText,
                genre: genre,
                genreKeyText: genreKeyText,
                discNo: discNo,
                year: year,
                compilation: compilation,
                trackNo: trackNo,
                durationMs: durationMs,
                sampleRate: sampleRate,
                bitDepth: bitDepth,
                artOffset: artOffset,
                artLength: artLength,
                artKind: artKind,
                folderArtUri: folderArtUri,
                folderArtSize: folderArtSize,
                folderArtMtime: folderArtMtime,
                audioFingerprint: audioFingerprint,
                reasonCode: reasonCode,
                scanVersion: scanVersion,
              ),
          withReferenceMapper: (p0) => p0
              .map(
                (e) => (
                  e.readTable<$TracksTable, TrackRow>(table),
                  $$TracksTableReferences(db, table, e),
                ),
              )
              .toList(),
          prefetchHooksCallback: ({sourceId = false}) {
            return PrefetchHooks(
              db: db,
              explicitlyWatchedTables: [],
              addJoins:
                  <
                    T extends TableManagerState<
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic,
                      dynamic
                    >
                  >(state) {
                    if (sourceId) {
                      state = state.withJoin(
                        currentTable: table,
                        currentColumn: table.sourceId,
                        referencedTable: $$TracksTableReferences._sourceIdTable(
                          db,
                        ),
                        referencedColumn: $$TracksTableReferences
                            ._sourceIdTable(db)
                            .id,
                      ) as T;
                    }

                    return state;
                  },
              getPrefetchedDataCallback: (items) async {
                return [];
              },
            );
          },
        ),
      );
}

typedef $$TracksTableProcessedTableManager =
    ProcessedTableManager<
      _$LibraryDatabase,
      $TracksTable,
      TrackRow,
      $$TracksTableFilterComposer,
      $$TracksTableOrderingComposer,
      $$TracksTableAnnotationComposer,
      $$TracksTableCreateCompanionBuilder,
      $$TracksTableUpdateCompanionBuilder,
      (TrackRow, $$TracksTableReferences),
      TrackRow,
      PrefetchHooks Function({bool sourceId})
    >;

class $LibraryDatabaseManager {
  final _$LibraryDatabase _db;
  $LibraryDatabaseManager(this._db);
  $$SourcesTableTableManager get sources =>
      $$SourcesTableTableManager(_db, _db.sources);
  $$TracksTableTableManager get tracks =>
      $$TracksTableTableManager(_db, _db.tracks);
}
