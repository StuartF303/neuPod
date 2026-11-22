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

/// File extensions the scanner lists as tracks: the spec's formats, plus
/// `.mp4`, which the firmware also accepts.
const audioExtensions = {
  'flac',
  'mp3',
  'm4a',
  'mp4',
  'aac',
  'wav',
  'ogg',
  'oga',
  'opus',
};

/// Extensions whose tags phase 1 reads; the others are listed by file name
/// until phase 3 adds their containers.
const _tagged = {'flac', 'mp3'};

/// The version of what a scan writes per row: the tag reader's fields and
/// rules and the sort key. Bump it when either changes; the next scan then
/// reads every file again instead of trusting unchanged size and mtime.
/// 1: phase 1 (FLAC and MP3 tags, title sort tags, firmware-cut sort key).
const scanVersion = 1;

const _scanVersionKey = 'scan_version';

/// The lower-case extension of [path], or ''.
String extensionOf(String path) {
  final slash = path.lastIndexOf('/');
  final dot = path.lastIndexOf('.');
  return dot > slash + 1 ? path.substring(dot + 1).toLowerCase() : '';
}

/// Minimal phase 1 scanner: walks every available folder with neupod_storage,
/// reads the tags of new or changed FLAC and MP3 files in a background
/// isolate, and writes tracks in batches.
///
/// - Files whose size and modification time are unchanged are not opened,
///   unless their last read failed transiently or the scan version changed.
/// - Tracks of files missing from a walk are deleted only when the walk was
///   complete (every folder listed); tracks of folders no longer held are
///   deleted; an unavailable folder keeps its tracks.
/// - Music files are only ever read.
final class LocalLibraryScanner implements LibraryScanner {
  LocalLibraryScanner({
    required this._folders,
    required this._storage,
    required this._db,
    this.chunk = 32,
    IsolateRunner? runIsolate,
  }) : _run = runIsolate ?? Isolate.run;

  final FolderAccess _folders;
  final NeupodStorage _storage;
  final LibraryDatabase _db;
  final IsolateRunner _run;

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

    // Rows read by an older reader or key are read again, whatever their
    // size and mtime. The version is stored once every folder was walked.
    final versionChanged = await _db.metaValue(_scanVersionKey) != scanVersion;
    var allComplete = true;

    for (final folder in folders) {
      if (!folder.available) {
        allComplete = false;
        continue; // its tracks stay until it is removed
      }
      final sourceId = await _sourceFor(folder);
      final known = {
        for (final t in await (_db.select(
          _db.tracks,
        )..where((t) => t.sourceId.equals(sourceId))).get())
          t.relPath: t,
      };
      final seen = <String>{};
      final pending = <StorageEntry>[];
      var complete = false;
      try {
        await for (final page in _storage.walk(folder.id)) {
          for (final e in page.entries) {
            if (!audioExtensions.contains(extensionOf(e.relPath))) continue;
            found++;
            seen.add(e.relPath);
            final k = known[e.relPath];
            if (!versionChanged && k != null && _unchanged(k, e)) continue;
            pending.add(e);
            if (pending.length >= chunk) {
              read += await _readAndStore(sourceId, pending);
              pending.clear();
            }
          }
          if (page.done) complete = page.complete;
          yield ScanProgress(filesFound: found, filesRead: read);
        }
      } on StorageException {
        complete = false; // access went away mid-walk: keep what the folder had
      }
      if (pending.isNotEmpty) read += await _readAndStore(sourceId, pending);

      if (complete) {
        final gone = known.keys.where((p) => !seen.contains(p)).toList();
        for (var i = 0; i < gone.length; i += 500) {
          final end = i + 500 > gone.length ? gone.length : i + 500;
          final part = gone.sublist(i, end);
          await (_db.delete(_db.tracks)..where(
                (t) => t.sourceId.equals(sourceId) & t.relPath.isIn(part),
              ))
              .go();
        }
      } else {
        allComplete = false;
      }
      yield ScanProgress(filesFound: found, filesRead: read);
    }
    if (allComplete) await _db.setMetaValue(_scanVersionKey, scanVersion);
    yield ScanProgress(filesFound: found, filesRead: read, done: true);
  }

  /// Same file as last time, and its last read did not fail transiently.
  static bool _unchanged(TrackRow k, StorageEntry e) =>
      k.size == e.size &&
      k.mtime == e.modifiedMs &&
      k.uri == e.uri &&
      !(UnreadableReason.fromCode(k.reasonCode)?.transient ?? false);

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
    try {
      for (final e in entries) {
        final ext = extensionOf(e.relPath);
        if (e.state == EntryState.notDownloaded) {
          jobs.add(_Job(ext, null, UnreadableReason.notDownloaded));
        } else if (!_tagged.contains(ext)) {
          jobs.add(_Job(ext, null, null));
        } else {
          try {
            jobs.add(_Job(ext, await _storage.openHandle(e), null));
          } on StorageException {
            jobs.add(_Job(ext, null, UnreadableReason.cannotOpen));
          }
        }
      }
    } catch (_) {
      _discardAll(
        jobs,
      ); // an unexpected failure while opening: close what is open
      rethrow;
    }
    List<_Result> results;
    try {
      results = await _parseInIsolate(_run, jobs);
    } catch (_) {
      // The isolate did not run (spawn failed): it never took the handles.
      _discardAll(jobs);
      results = [
        for (final _ in jobs) const _Result(null, UnreadableReason.cannotRead),
      ];
    }
    await _db.batch((b) {
      for (var i = 0; i < entries.length; i++) {
        final e = entries[i];
        final r = results[i];
        final title = r.tags?.title ?? '';
        final titleSort = r.tags?.titleSort ?? '';
        final row = TracksCompanion.insert(
          sourceId: sourceId,
          relPath: e.relPath,
          uri: e.uri,
          size: e.size,
          mtime: e.modifiedMs,
          codec: r.tags?.codec ?? jobs[i].ext,
          title: title,
          titleSort: titleSort,
          titleSortKey: songSortKey(
            title: title,
            titleSort: titleSort,
            fileName: _fileName(e.relPath),
          ),
          artist: r.tags?.artist ?? '',
          album: r.tags?.album ?? '',
          trackNo: r.tags?.trackNo ?? 0,
          durationMs: r.tags?.durationMs ?? 0,
          sampleRate: r.tags?.sampleRate ?? 0,
          bitDepth: r.tags?.bitDepth ?? 0,
          reasonCode: Value(r.reason?.code),
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

  static void _discardAll(List<_Job> jobs) {
    for (final j in jobs) {
      final h = j.handle;
      if (h != null && !h.isUsed) h.discard();
    }
  }

  static String _fileName(String relPath) {
    final slash = relPath.lastIndexOf('/');
    return slash < 0 ? relPath : relPath.substring(slash + 1);
  }
}

/// One file to read in the isolate: a handle to open, or a reason it is not
/// read.
final class _Job {
  const _Job(this.ext, this.handle, this.reason);

  final String ext;
  final ReaderHandle? handle;
  final UnreadableReason? reason;
}

final class _Result {
  const _Result(this.tags, this.reason);

  final AudioTags? tags;
  final UnreadableReason? reason;
}

/// Runs a computation in a new isolate: [Isolate.run], or a stand-in in
/// tests (for example one that fails to start).
typedef IsolateRunner = Future<R> Function<R>(R Function() computation);

/// Top level, so the isolate closure captures only [jobs] (not the scanner
/// and its database).
Future<List<_Result>> _parseInIsolate(IsolateRunner run, List<_Job> jobs) =>
    run(() => _parseAll(jobs));

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
    // Open failed (not seekable) or a read failed part-way.
    return const _Result(null, UnreadableReason.cannotRead);
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
