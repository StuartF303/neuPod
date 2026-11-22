// SPDX-License-Identifier: MPL-2.0

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

/// Inserts a track as the scanner stores it: the sort key is
/// [songSortKey] of the title (or, when it is empty, the file name with its
/// extension) and the sort tag, as `index_file` in `lib_build.c` builds it.
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
}) {
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
            titleText: title.codeUnits,
            titleSortText: titleSort.codeUnits,
            fileName: fileName,
          ),
          artist: artist,
          album: album,
          trackNo: trackNo,
          durationMs: durationMs,
          sampleRate: 44100,
          bitDepth: 16,
          reasonCode: Value(reason),
          scanVersion: 1,
        ),
      );
}
