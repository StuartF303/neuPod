// SPDX-License-Identifier: MPL-2.0

import 'dart:isolate';

import 'package:drift/drift.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/collation.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// File extensions the scanner lists as tracks (the spec's formats).
const audioExtensions = {
  'flac',
  'mp3',
  'm4a',
  'aac',
  'wav',
  'ogg',
  'oga',
  'opus',
};

/// Extensions whose tags phase 1 reads; the others are listed by file name
/// until phase 3 adds their containers.
const _tagged = {'flac', 'mp3'};

/// The lower-case extension of [path], or ''.
String extensionOf(String path) {
  final slash = path.lastIndexOf('/');
  final dot = path.lastIndexOf('.');
  return dot > slash + 1 ? path.substring(dot + 1).toLowerCase() : '';
}

/// Minimal phase 1 scanner: walks every available folder with neupod_storage,
/// reads the tags of new or changed FLAC and MP3 files in a background
/// isolate, and writes tracks in batches. Files whose size and modification
/// time are unchanged are not opened. Tracks of files that have gone, and of
/// folders no longer held, are deleted. Music files are only ever read.
final class LocalLibraryScanner implements LibraryScanner {
  LocalLibraryScanner({
    required this._folders,
    required this._storage,
    required this._db,
    this.chunk = 32,
  });

  final FolderAccess _folders;
  final NeupodStorage _storage;
  final LibraryDatabase _db;

  /// Files read per isolate run and written per batch.
  final int chunk;

  static const _kind = 'local_folder';

  @override
  Stream<ScanProgress> scan() async* {
    var found = 0, read = 0;
    final folders = await _folders.folders();

    // Removing a folder removes its tracks, and nothing else.
    final held = folders.map((f) => f.id).toList();
    await (_db.delete(
      _db.sources,
    )..where((s) => s.kind.equals(_kind) & s.uriOrBookmark.isNotIn(held))).go();

    for (final folder in folders) {
      if (!folder.available) continue; // its tracks stay until it is removed
      final sourceId = await _sourceFor(folder);
      final known = {
        for (final t in await (_db.select(
          _db.tracks,
        )..where((t) => t.sourceId.equals(sourceId))).get())
          t.relPath: t,
      };
      final seen = <String>{};
      final pending = <StorageEntry>[];
      try {
        await for (final page in _storage.walk(folder.id)) {
          for (final e in page) {
            if (!audioExtensions.contains(extensionOf(e.relPath))) continue;
            found++;
            seen.add(e.relPath);
            final k = known[e.relPath];
            if (k != null &&
                k.size == e.size &&
                k.mtime == e.modifiedMs &&
                k.uri == e.uri) {
              continue;
            }
            pending.add(e);
            if (pending.length >= chunk) {
              read += await _readAndStore(sourceId, pending);
              pending.clear();
            }
          }
          yield ScanProgress(filesFound: found, filesRead: read);
        }
      } on StorageException {
        continue; // access went away mid-walk: keep what the folder had
      }
      if (pending.isNotEmpty) read += await _readAndStore(sourceId, pending);

      final gone = known.keys.where((p) => !seen.contains(p)).toList();
      for (var i = 0; i < gone.length; i += 500) {
        final part = gone.sublist(
          i,
          i + 500 > gone.length ? gone.length : i + 500,
        );
        await (_db.delete(
              _db.tracks,
            )..where((t) => t.sourceId.equals(sourceId) & t.relPath.isIn(part)))
            .go();
      }
      yield ScanProgress(filesFound: found, filesRead: read);
    }
    yield ScanProgress(filesFound: found, filesRead: read, done: true);
  }

  Future<int> _sourceFor(LibraryFolder folder) async {
    final existing = await (_db.select(
      _db.sources,
    )..where((s) => s.uriOrBookmark.equals(folder.id))).getSingleOrNull();
    if (existing != null) {
      if (existing.displayName != folder.name) {
        await (_db.update(_db.sources)..where((s) => s.id.equals(existing.id)))
            .write(SourcesCompanion(displayName: Value(folder.name)));
      }
      return existing.id;
    }
    return _db
        .into(_db.sources)
        .insert(
          SourcesCompanion.insert(
            kind: _kind,
            uriOrBookmark: folder.id,
            displayName: folder.name,
          ),
        );
  }

  /// Reads [entries] and upserts them; returns how many were read.
  Future<int> _readAndStore(int sourceId, List<StorageEntry> entries) async {
    final jobs = <_Job>[];
    for (final e in entries) {
      final ext = extensionOf(e.relPath);
      if (e.state == EntryState.notDownloaded) {
        jobs.add(_Job(ext, null, 'Not downloaded'));
      } else if (!_tagged.contains(ext)) {
        jobs.add(_Job(ext, null, null));
      } else {
        try {
          jobs.add(_Job(ext, await _storage.openHandle(e.uri), null));
        } on StorageException {
          jobs.add(_Job(ext, null, "Can't open"));
        }
      }
    }
    final results = await _parseInIsolate(jobs);
    await _db.batch((b) {
      for (var i = 0; i < entries.length; i++) {
        final e = entries[i];
        final r = results[i];
        final title = r.tags?.title ?? '';
        final row = TracksCompanion.insert(
          sourceId: sourceId,
          relPath: e.relPath,
          uri: e.uri,
          size: e.size,
          mtime: e.modifiedMs,
          codec: r.tags?.codec ?? jobs[i].ext,
          title: title,
          titleSortKey: collateKey(Track.displayTitleOf(title, e.relPath)),
          artist: r.tags?.artist ?? '',
          album: r.tags?.album ?? '',
          trackNo: r.tags?.trackNo ?? 0,
          durationMs: r.tags?.durationMs ?? 0,
          sampleRate: r.tags?.sampleRate ?? 0,
          bitDepth: r.tags?.bitDepth ?? 0,
          unreadableReason: Value(r.reason),
        );
        b.insert(
          _db.tracks,
          row,
          onConflict: DoUpdate(
            (_) => row,
            target: [_db.tracks.sourceId, _db.tracks.relPath],
          ),
        );
      }
    });
    return entries.length;
  }
}

/// One file to read in the isolate: a handle to open, or a reason it is not
/// read.
final class _Job {
  const _Job(this.ext, this.handle, this.reason);

  final String ext;
  final ReaderHandle? handle;
  final String? reason;
}

final class _Result {
  const _Result(this.tags, this.reason);

  final AudioTags? tags;
  final String? reason;
}

/// Top level, so the isolate closure captures only [jobs] (not the scanner
/// and its database).
Future<List<_Result>> _parseInIsolate(List<_Job> jobs) =>
    Isolate.run(() => _parseAll(jobs));

/// Runs in a background isolate. Never throws: every failure is a reason.
List<_Result> _parseAll(List<_Job> jobs) => [for (final j in jobs) _parse(j)];

_Result _parse(_Job j) {
  final handle = j.handle;
  if (handle == null) return _Result(null, j.reason);
  StorageReader? reader;
  try {
    reader = handle.open();
    return _Result(readTags(_ReaderSource(reader), j.ext), null);
  } on TagReadException catch (e) {
    return _Result(null, e.reason);
  } catch (_) {
    return _Result(null, "Can't read");
  } finally {
    reader?.close();
  }
}

final class _ReaderSource implements ByteSource {
  _ReaderSource(this._r);

  final StorageReader _r;

  @override
  int get length => _r.length;

  @override
  Uint8List readAt(int offset, int count) => _r.readAt(offset, count);
}
