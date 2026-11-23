// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/collation.dart';

/// Inserts a source named [name] and returns its id.
Future<int> addSource(LibraryDatabase db, {String name = 'Music'}) => db
    .into(db.sources)
    .insert(
      SourcesCompanion.insert(
        kind: 'local_folder',
        uriOrBookmark: 'src-$name',
        displayName: name,
      ),
    );

/// Inserts a track as the scanner stores it for a UTF-8 tag: the sort key is
/// [songSortKey] of the title (or, when it is empty, the file name with its
/// extension) and the sort tag, as `index_file` in `lib_build.c` builds it,
/// each as the firmware's parser holds it (UTF-8, cut to `title[64]` and
/// `sort_title[32]`). The firmware's text of the artist and album is
/// [artistKeyText] / [albumKeyText], or by default the tag's UTF-8 cut to 63
/// bytes; [legacy] leaves both null, as in a row from before schema version
/// 2.
Future<int> addTrack(
  LibraryDatabase db, {
  required int sourceId,
  required String relPath,
  String title = '',
  String titleSort = '',
  String artist = '',
  String album = '',
  int trackNo = 0,
  int durationMs = 0,
  int? reason,
  String codec = 'flac',
  List<int>? artistKeyText,
  List<int>? albumKeyText,
  bool legacy = false,
  int? artOffset,
  int? artLength,
  int? artKind,
  String? folderArtUri,
  int? folderArtSize,
  int? folderArtMtime,
}) {
  Value<Uint8List?> held(List<int>? given, String tag) => Value(
    legacy
        ? null
        : Uint8List.fromList(given ?? firmwareCut(utf8.encode(tag), 64)),
  );
  final fileName = relPath.substring(relPath.lastIndexOf('/') + 1);
  return db
      .into(db.tracks)
      .insert(
        TracksCompanion.insert(
          sourceId: sourceId,
          relPath: relPath,
          uri: 'file:///music/$relPath',
          size: 1,
          mtime: 1,
          codec: codec,
          title: title,
          titleSort: titleSort,
          titleSortKey: songSortKey(
            titleText: firmwareCut(utf8.encode(title), 64),
            titleSortText: firmwareCut(utf8.encode(titleSort), 32),
            fileName: fileName,
          ),
          artist: artist,
          album: album,
          artistKeyText: held(artistKeyText, artist),
          albumKeyText: held(albumKeyText, album),
          trackNo: trackNo,
          durationMs: durationMs,
          sampleRate: 44100,
          bitDepth: 16,
          reasonCode: Value(reason),
          artOffset: Value(artOffset),
          artLength: Value(artLength),
          artKind: Value(artKind),
          folderArtUri: Value(folderArtUri),
          folderArtSize: Value(folderArtSize),
          folderArtMtime: Value(folderArtMtime),
          scanVersion: 1,
        ),
      );
}
