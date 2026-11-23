// SPDX-License-Identifier: MPL-2.0

import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:drift/drift.dart';
import 'package:flutter/foundation.dart' show visibleForTesting;
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/data/scan/tag_reader.dart';
import 'package:neupod/domain/library/art_rules.dart';
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

/// Extensions the firmware's scanner parses (`index_file` in lib_build.c:
/// `.wav`, `.flac`, `.mp3`, and the rest of what it accepts as MP4); the
/// others are listed by file name.
const _tagged = {'flac', 'mp3', 'm4a', 'mp4', 'wav'};

/// The version of what a scan writes per row: the tag reader's fields and
/// rules and the sort key. Bump it when either changes: a row written with
/// another version is read again, whatever its size and mtime, and only once
/// (the version is stored on the row).
/// 1: phase 1 (FLAC and MP3 tags, title sort tags, the firmware's key).
/// 2: the firmware's text of the artist and album tags (schema version 2),
///    so rows from version 1 are read again to fill it.
/// 3: the library tags (schema version 3): album artist, sort tags, genre,
///    disc, year, compilation flag; the track number as the firmware reads
///    it (none from a FLAC comment block over its scratch).
/// 4: MP4 (ALAC, AAC) tags and duration, WAV duration.
/// 5: where the embedded picture is (schema version 4), so rows from earlier
///    versions are read again to find it.
const scanVersion = 5;

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
/// - Each folder's cover file (`cover.jpg`, `folder.jpg`, `front.jpg`: the
///   firmware's `art_folder_pick`) is noted on every track of the folder after
///   the walk, whether or not the track changed, so adding, changing or
///   removing a cover file needs no change to the music files.
final class LocalLibraryScanner implements LibraryScanner {
  LocalLibraryScanner({
    required this._folders,
    required this._storage,
    required this._db,
    this.chunk = 32,
    this.version = scanVersion,
    IsolateRunner? runIsolate,
    @visibleForTesting ReaderOpener? openReader,
  }) : _run = runIsolate ?? Isolate.run,
       _openReader = openReader ?? _openHandle;

  /// The [scanVersion] rows are written with (other values in tests only).
  final int version;

  final FolderAccess _folders;
  final NeupodStorage _storage;
  final LibraryDatabase _db;
  final IsolateRunner _run;
  final ReaderOpener _openReader;

  static StorageReader _openHandle(ReaderHandle h) => h.open();

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
      // Cover file names by folder (the folder's relative path, '' for the
      // root), from every file the walk lists.
      final covers = <String, List<StorageEntry>>{};
      var complete = false;
      try {
        await for (final page in _storage.walk(folder.id)) {
          for (final e in page.entries) {
            if (artFolderRank(_fileName(e.relPath)) != 0) {
              (covers[_folderOf(e.relPath)] ??= []).add(e);
              continue;
            }
            if (!audioExtensions.contains(extensionOf(e.relPath))) continue;
            found++;
            seen.add(e.relPath);
            final k = known[e.relPath];
            if (k != null && _unchanged(k, e)) continue;
            pending.add(e);
            if (pending.length >= chunk) {
              read += await _readAndStore(sourceId, pending, known);
              pending.clear();
            }
          }
          if (page.done) complete = page.complete;
          yield ScanProgress(filesFound: found, filesRead: read);
        }
      } on StorageException {
        complete = false; // access went away mid-walk: keep what the folder had
      }
      if (pending.isNotEmpty) {
        read += await _readAndStore(sourceId, pending, known);
      }

      await _storeCovers(sourceId, covers, complete: complete);

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
      }
      yield ScanProgress(filesFound: found, filesRead: read);
    }
    yield ScanProgress(filesFound: found, filesRead: read, done: true);
  }

  /// Notes each track's folder cover file (`art_folder_pick` over the
  /// folder's cover files). The firmware picks the first of the best rank in
  /// its folder listing; the app cannot see the card's listing order, so names
  /// of the same rank (`Folder.jpg` and `folder.jpg` on a case-sensitive
  /// file system) are taken in byte order of their UTF-8 names, as the walk
  /// order is (see `walkCompare` in the library view). A cover file that is
  /// not downloaded (iCloud) is not read (owner decision 7): the folder then
  /// has none, which is what the device shows for a cover file it cannot
  /// decode. After an incomplete walk a folder's cover is only set, never
  /// cleared, since the folder may not have been listed.
  Future<void> _storeCovers(
    int sourceId,
    Map<String, List<StorageEntry>> covers, {
    required bool complete,
  }) async {
    final picked = <String, StorageEntry?>{};
    for (final MapEntry(key: dir, value: entries) in covers.entries) {
      entries.sort(
        (a, b) => compareKeys(
          utf8.encode(_fileName(a.relPath)),
          utf8.encode(_fileName(b.relPath)),
        ),
      );
      final best = artFolderPick(entries.map((e) => _fileName(e.relPath)));
      final e = best == null
          ? null
          : entries.firstWhere((e) => _fileName(e.relPath) == best.$1);
      picked[dir] = e == null || e.state == EntryState.notDownloaded ? null : e;
    }
    final rows = await (_db.select(
      _db.tracks,
    )..where((t) => t.sourceId.equals(sourceId))).get();
    await _db.batch((b) {
      for (final t in rows) {
        final dir = _folderOf(t.relPath);
        if (!complete && picked[dir] == null) continue;
        final e = picked[dir];
        if (t.folderArtUri == e?.uri &&
            t.folderArtSize == e?.size &&
            t.folderArtMtime == e?.modifiedMs) {
          continue;
        }
        b.update(
          _db.tracks,
          TracksCompanion(
            folderArtUri: Value(e?.uri),
            folderArtSize: Value(e?.size),
            folderArtMtime: Value(e?.modifiedMs),
          ),
          where: (r) => r.id.equals(t.id),
        );
      }
    });
  }

  /// Same file as last time, read by this version, and its last read did not
  /// fail transiently.
  bool _unchanged(TrackRow k, StorageEntry e) =>
      k.scanVersion == version &&
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

  /// Reads [entries] and upserts them; returns how many were read. A
  /// transient failure on a file already in [known] keeps its stored tags and
  /// only records the reason (so it is retried next time).
  Future<int> _readAndStore(
    int sourceId,
    List<StorageEntry> entries,
    Map<String, TrackRow> known,
  ) async {
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
          } on StorageException catch (e) {
            jobs.add(_Job(ext, null, storageReason(e, opening: true)));
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
      results = await _parseInIsolate(_run, jobs, _openReader);
    } on IsolateSpawnException {
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
        final previous = known[e.relPath];
        if (r.reason != null && r.reason!.transient && previous != null) {
          b.update(
            _db.tracks,
            TracksCompanion(reasonCode: Value(r.reason!.code)),
            where: (t) => t.id.equals(previous.id),
          );
          continue;
        }
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
            titleText: r.tags?.titleKeyText ?? const [],
            titleSortText: r.tags?.titleSortKeyText ?? const [],
            fileName: _fileName(e.relPath),
          ),
          artist: r.tags?.artist ?? '',
          album: r.tags?.album ?? '',
          artistKeyText: Value(
            Uint8List.fromList(r.tags?.artistKeyText ?? const []),
          ),
          albumKeyText: Value(
            Uint8List.fromList(r.tags?.albumKeyText ?? const []),
          ),
          albumArtist: Value(r.tags?.albumArtist ?? ''),
          albumArtistKeyText: Value(_blob(r.tags?.albumArtistKeyText)),
          artistSortKeyText: Value(_blob(r.tags?.artistSortKeyText)),
          albumSortKeyText: Value(_blob(r.tags?.albumSortKeyText)),
          albumArtistSortKeyText: Value(_blob(r.tags?.albumArtistSortKeyText)),
          genre: Value(r.tags?.genre ?? ''),
          genreKeyText: Value(_blob(r.tags?.genreKeyText)),
          discNo: Value(r.tags?.discNo ?? 0),
          year: Value(r.tags?.year ?? 0),
          compilation: Value(r.tags?.compilation ?? false),
          trackNo: r.tags?.trackNo ?? 0,
          artOffset: Value(r.tags?.art?.offset),
          artLength: Value(r.tags?.art?.length),
          artKind: Value(r.tags?.art?.kind.code ?? ArtKind.none.code),
          durationMs: r.tags?.durationMs ?? 0,
          sampleRate: r.tags?.sampleRate ?? 0,
          bitDepth: r.tags?.bitDepth ?? 0,
          reasonCode: Value(r.reason?.code),
          scanVersion: version,
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

  static Uint8List _blob(List<int>? b) => Uint8List.fromList(b ?? const []);

  static String _fileName(String relPath) {
    final slash = relPath.lastIndexOf('/');
    return slash < 0 ? relPath : relPath.substring(slash + 1);
  }

  /// The folder of [relPath]: its path without the file name, '' at the root.
  static String _folderOf(String relPath) {
    final slash = relPath.lastIndexOf('/');
    return slash < 0 ? '' : relPath.substring(0, slash);
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

/// Turns a [ReaderHandle] into a reader, in the background isolate:
/// `handle.open()`, or a stand-in in tests (it must be sendable).
typedef ReaderOpener = StorageReader Function(ReaderHandle handle);

/// Top level, so the isolate closure captures only [jobs] and [open] (not the
/// scanner and its database).
Future<List<_Result>> _parseInIsolate(
  IsolateRunner run,
  List<_Job> jobs,
  ReaderOpener open,
) => run(() => _parseAll(jobs, open));

/// Runs in a background isolate. Never throws: every failure is a reason.
List<_Result> _parseAll(List<_Job> jobs, ReaderOpener open) => [
  for (final j in jobs) _parse(j, open),
];

_Result _parse(_Job j, ReaderOpener open) {
  final handle = j.handle;
  if (handle == null) return _Result(null, j.reason);
  final (tags, reason) = readWithReader(() => open(handle), j.ext);
  return _Result(tags, reason);
}

/// neupod_storage error codes after which the provider can never serve the
/// file, so retrying is pointless until it changes. The codes the plugin
/// reports on the read path are `open-failed`, `detached` and `unsupported`
/// (Android `NeupodStoragePlugin.kt` openFd; iOS `NeupodStoragePlugin.swift`
/// openFd), `not-downloaded` (the Dart side) and pigeon's `channel-error`;
/// walks add `root-unavailable` and `busy`. Of those only `unsupported` is
/// lasting: the others mean access is missing for now (a provider offline, a
/// grant lost, the engine detaching) and may come back.
const lastingStorageCodes = {'unsupported'};

/// The reason stored for a [StorageException] while [opening] a file or
/// reading it: `not-downloaded` is [UnreadableReason.notDownloaded], a
/// [lastingStorageCodes] code is [UnreadableReason.damaged] (not retried while
/// the file is unchanged), anything else is transient and retried next scan.
UnreadableReason storageReason(StorageException e, {required bool opening}) {
  if (e.code == 'not-downloaded') return UnreadableReason.notDownloaded;
  if (lastingStorageCodes.contains(e.code)) return UnreadableReason.damaged;
  return opening ? UnreadableReason.cannotOpen : UnreadableReason.cannotRead;
}

/// Opens a reader with [open] and reads its tags as [ext]; closes it. Never
/// throws: every failure is a reason. Transient failures (`cannotRead`) keep
/// the stored tags and are retried on the next scan.
@visibleForTesting
(AudioTags?, UnreadableReason?) readWithReader(
  StorageReader Function() open,
  String ext,
) {
  StorageReader? reader;
  try {
    reader = open();
    return (readTags(_ReaderSource(reader), ext), null);
  } on TagReadException catch (e) {
    // What the parser read before it failed, as the firmware keeps it.
    return (e.partial, e.reason);
  } on FileSystemException {
    // Open failed (not seekable, gone) or a read failed part-way: may pass.
    return (null, UnreadableReason.cannotRead);
  } on StorageException catch (e) {
    // The storage layer failed: transient unless its code says it is not.
    return (null, storageReason(e, opening: false));
  } catch (_) {
    // Anything else is the reader meeting bytes it cannot handle.
    return (null, UnreadableReason.damaged);
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
