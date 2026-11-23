// SPDX-License-Identifier: MPL-2.0

/// The art cache (specification, "How it is built": data layer; decision
/// 0009): turns an [ArtRef]'s pictures into small JPEG files in the app's
/// private support directory, which the screens draw and the lock screen
/// shows.
///
/// - **Which picture.** The [ArtRef]'s sources in order, the first that the
///   firmware's decoder would take ([firmwareJpegCheck]) and that decodes
///   (`find_and_decode` in `app/runtime/art_service.c`).
/// - **Sizes.** Two per picture, never the original: [ArtSize.thumb] for
///   list rows, the album header and the Music menu's previews, and
///   [ArtSize.full] for Home, Now Playing, Cover Flow and the lock screen.
///   Shrunk to fit, aspect ratio kept, never enlarged (as `art_decode` does).
/// - **Keys.** A file is named after a hash of [ArtRef.cacheKey], which holds
///   each source's URI, size, modification time and place in its file: a
///   changed file is a new key, and the old entry ages out.
/// - **Bound.** At most [ArtCache.maxBytes] on disk: the least recently used
///   files (by modification time, which a hit refreshes) go first.
/// - **Off the UI isolate.** Reading, decoding, resizing and encoding run in
///   a background isolate, one picture at a time, so a decode never blocks a
///   frame and only one full-size picture is in memory.
/// - **Read-only.** Music folders are only read, through the folder-access
///   plugin; the cache writes only to its own directory.
library;

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:image/image.dart' as img;
import 'package:neupod/data/art/firmware_jpeg.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// Opens a source for reading in a background isolate: a sendable handle.
typedef ArtHandleOpener = Future<ReaderHandle> Function(ArtFile file);

/// Turns a handle into a reader, in the background isolate (tests may stand
/// in; it must be sendable).
typedef ArtReaderOpener = StorageReader Function(ReaderHandle handle);

/// Runs a computation in another isolate ([Isolate.run], or a stand-in).
typedef ArtIsolateRunner = Future<R> Function<R>(R Function() computation);

/// The handle opener over neupod_storage: the source as a downloaded entry.
ArtHandleOpener storageArtOpener(NeupodStorage storage) =>
    (f) => storage.openHandle(
      StorageEntry(
        relPath: '',
        uri: f.uri,
        size: f.size,
        modifiedMs: f.mtime,
        state: EntryState.local,
      ),
    );

/// Largest picture read (an embedded picture or a cover file); bigger ones
/// are skipped as damaged. The firmware has no byte limit, only its
/// 5120-pixel one, which a picture this large would almost surely exceed.
const maxArtBytes = 32 * 1024 * 1024;

/// JPEG quality of the cached files.
const _quality = 88;

final class ArtCache {
  ArtCache({
    required this._directory,
    required this._open,
    this.maxBytes = 64 * 1024 * 1024,
    ArtIsolateRunner? run,
    ArtReaderOpener? openReader,
  }) : _run = run ?? Isolate.run,
       _openReader = openReader ?? _openHandle;

  final Future<String> Function() _directory;
  final ArtHandleOpener _open;
  final ArtIsolateRunner _run;
  final ArtReaderOpener _openReader;

  /// The most the cache keeps on disk.
  final int maxBytes;

  static StorageReader _openHandle(ReaderHandle h) => h.open();

  Future<String>? _dir;
  final _inFlight = <String, Future<bool>>{};

  /// Keys none of whose pictures decode, until the app restarts (a picture
  /// that could not be opened is not remembered: access may come back).
  final _none = HashSet<String>();

  /// One picture at a time.
  Future<void> _queue = Future.value();

  Future<String> _directoryPath() => _dir ??= () async {
    final d = await _directory();
    await Directory(d).create(recursive: true);
    return d;
  }();

  /// The file name for [art] at [size], or null when it has no sources.
  static String? fileName(ArtRef art, ArtSize size) {
    final k = art.cacheKey;
    if (k == null) return null;
    final h = sha1.convert(utf8.encode(k)).toString().substring(0, 32);
    return '$h.${size.tag}.jpg';
  }

  /// Where [art] at [size] is or will be, or null when it has no sources.
  /// The file may not exist yet: [file] makes it.
  Future<String?> pathFor(ArtRef art, ArtSize size) async {
    final name = fileName(art, size);
    if (name == null) return null;
    final path = '${await _directoryPath()}/$name';
    _byPath[path] = art;
    return path;
  }

  /// The art each path [pathFor] gave stands for, so [warm] can make it.
  final _byPath = <String, ArtRef>{};

  /// Makes the files at [paths] (given by [pathFor]) that are not there yet,
  /// in order, one at a time. For the lock screen, which reads a queue item's
  /// art file when the item starts. Never throws.
  Future<void> warm(Iterable<String> paths) async {
    for (final p in paths) {
      final art = _byPath[p];
      if (art == null || File(p).existsSync()) continue;
      try {
        await file(art, ArtSize.full);
      } on Object {
        // The placeholder (iOS: the file's own embedded art) stands.
      }
    }
  }

  /// The cached image of [art] at [size], made if need be; null when no
  /// picture of it decodes (or it has none). A ready [ArtRef.path] is
  /// returned as it is.
  Future<String?> file(ArtRef art, ArtSize size) async {
    if (art.sources.isEmpty) return art.path;
    final key = art.cacheKey!;
    if (_none.contains(key)) return null;
    final path = (await pathFor(art, size))!;
    final f = File(path);
    if (f.existsSync()) {
      _touch(f);
      return path;
    }
    // The callback must not return the removed future: whenComplete would
    // wait on it, which is itself.
    final made = await (_inFlight[key] ??= _make(art).whenComplete(() {
      _inFlight.remove(key);
    }));
    return made && f.existsSync() ? path : null;
  }

  Future<bool> _make(ArtRef art) {
    final c = Completer<bool>();
    _queue = _queue.then((_) async {
      try {
        c.complete(await _render(art));
      } catch (e, s) {
        c.completeError(e, s);
      }
    });
    return c.future;
  }

  Future<bool> _render(ArtRef art) async {
    final key = art.cacheKey!;
    final dir = await _directoryPath();
    final thumb = '$dir/${fileName(art, ArtSize.thumb)}';
    final full = '$dir/${fileName(art, ArtSize.full)}';
    var openFailed = false;
    for (final src in art.sources) {
      ReaderHandle handle;
      try {
        handle = await _open(src);
      } on Object {
        openFailed = true; // not there now: try the next, remember nothing
        continue;
      }
      ArtOutcome outcome;
      try {
        outcome = await _renderInIsolate(
          _run,
          _openReader,
          handle,
          src.offset,
          src.length,
          thumb,
          full,
        );
      } on IsolateSpawnException {
        if (!handle.isUsed) handle.discard();
        openFailed = true;
        continue;
      }
      if (outcome == ArtOutcome.ok) {
        await _evict(dir);
        return true;
      }
      if (outcome == ArtOutcome.unreadable) openFailed = true;
    }
    if (!openFailed) _none.add(key);
    return false;
  }

  static void _touch(File f) {
    try {
      f.setLastModifiedSync(DateTime.now());
    } on FileSystemException {
      // Only the eviction order suffers.
    }
  }

  int _writesSinceCheck = 0;

  /// Keeps the directory under [maxBytes]: oldest first.
  Future<void> _evict(String dir) async {
    // Listing the directory costs a little: look every few writes, and the
    // bound is overshot by at most those few pictures.
    if (++_writesSinceCheck < 8 && maxBytes > 4 * 1024 * 1024) return;
    _writesSinceCheck = 0;
    await evictNow();
  }

  /// Brings the cache under [maxBytes] now (also run every few writes).
  Future<void> evictNow() async {
    final dir = Directory(await _directoryPath());
    final files = <(File, int, DateTime)>[];
    var total = 0;
    await for (final e in dir.list(followLinks: false)) {
      if (e is! File) continue;
      try {
        final st = e.statSync();
        files.add((e, st.size, st.modified));
        total += st.size;
      } on FileSystemException {
        continue;
      }
    }
    if (total <= maxBytes) return;
    files.sort((a, b) => a.$3.compareTo(b.$3));
    for (final (f, size, _) in files) {
      if (total <= maxBytes) break;
      try {
        f.deleteSync();
        total -= size;
      } on FileSystemException {
        continue;
      }
    }
  }
}

/// Top level, so the isolate's closure captures only its arguments (not the
/// cache).
Future<ArtOutcome> _renderInIsolate(
  ArtIsolateRunner run,
  ArtReaderOpener open,
  ReaderHandle handle,
  int offset,
  int? length,
  String thumb,
  String full,
) => run(
  () => renderArt(
    () => open(handle),
    offset: offset,
    length: length,
    thumbPath: thumb,
    fullPath: full,
  ),
);

/// How one source went ([renderArt]).
enum ArtOutcome {
  ok,

  /// Read failed (access, I/O): may work later.
  unreadable,

  /// Not a picture the firmware's decoder takes, or damaged: never will.
  undecodable,
}

/// Reads the picture of [length] bytes at [offset] (the whole file when
/// null) from the reader [open] makes, checks it as the firmware would,
/// decodes it and writes the two sizes to [thumbPath] and [fullPath].
/// Runs in a background isolate. Never throws.
ArtOutcome renderArt(
  StorageReader Function() open, {
  required int offset,
  required int? length,
  required String thumbPath,
  required String fullPath,
}) {
  Uint8List bytes;
  StorageReader? reader;
  try {
    reader = open();
    final n = length ?? (reader.length - offset);
    if (n <= 0 || n > maxArtBytes || offset < 0) return ArtOutcome.undecodable;
    bytes = reader.readAt(offset, n);
    if (bytes.length != n) return ArtOutcome.undecodable; // cut short
  } on Object {
    return ArtOutcome.unreadable;
  } finally {
    try {
      reader?.close();
    } on Object {
      // Closing a reader that failed to open.
    }
  }
  final picture = decodeArt(bytes);
  if (picture == null) return ArtOutcome.undecodable;
  try {
    final full = shrinkToFit(picture, ArtSize.full.pixels);
    final thumb = shrinkToFit(full, ArtSize.thumb.pixels);
    _writeAtomically(fullPath, img.encodeJpg(full, quality: _quality));
    _writeAtomically(thumbPath, img.encodeJpg(thumb, quality: _quality));
    return ArtOutcome.ok;
  } on Object {
    return ArtOutcome.unreadable; // the cache directory: may work later
  }
}

/// The picture in [bytes] if the firmware's decoder would take it and it
/// decodes; otherwise null.
img.Image? decodeArt(Uint8List bytes) {
  if (firmwareJpegCheck(bytes) != FirmwareJpeg.ok) return null;
  try {
    return img.JpegDecoder().decode(bytes);
  } on Object {
    return null; // damaged data: ART_E_FORMAT
  }
}

/// [picture] shrunk so its longer side is at most [box] (`art_decode`: aspect
/// ratio kept, never enlarged).
img.Image shrinkToFit(img.Image picture, int box) {
  final w = picture.width, h = picture.height;
  if (w <= box && h <= box) return picture;
  final wide = w >= h;
  final tw = wide ? box : (w * box ~/ h).clamp(1, box);
  final th = wide ? (h * box ~/ w).clamp(1, box) : box;
  return img.copyResize(
    picture,
    width: tw,
    height: th,
    interpolation: img.Interpolation.average,
  );
}

void _writeAtomically(String path, Uint8List data) {
  final tmp = File('$path.part');
  tmp.writeAsBytesSync(data, flush: true);
  tmp.renameSync(path);
}
