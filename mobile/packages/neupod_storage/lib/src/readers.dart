// SPDX-License-Identifier: MPL-2.0

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

/// Random-access, read-only bytes of one file: the interface the tag reader
/// reads through (decision 0003). Synchronous, for use in a background
/// isolate; never call it on the UI isolate for more than a header.
abstract interface class StorageReader {
  /// The file's size in bytes.
  int get length;

  /// Reads up to [count] bytes at [offset]. Returns fewer at the end of the
  /// file and an empty list past it. Never reads more than asked.
  Uint8List readAt(int offset, int count);

  /// Releases the file. Further reads throw.
  void close();
}

/// A [StorageReader] over a `dart:io` file (iOS `file://` URIs, tests).
final class FileStorageReader implements StorageReader {
  FileStorageReader._(this._file, this.length);

  /// Opens [uri] read-only.
  factory FileStorageReader.open(Uri uri) {
    final file = File.fromUri(uri).openSync();
    try {
      return FileStorageReader._(file, file.lengthSync());
    } catch (_) {
      file.closeSync();
      rethrow;
    }
  }

  final RandomAccessFile _file;
  bool _closed = false;

  @override
  final int length;

  @override
  Uint8List readAt(int offset, int count) {
    if (_closed) throw StateError('reader is closed');
    if (offset < 0 || count < 0) throw RangeError('negative offset or count');
    if (offset >= length || count == 0) return Uint8List(0);
    _file.setPositionSync(offset);
    return _file.readSync(count);
  }

  @override
  void close() {
    if (_closed) return;
    _closed = true;
    _file.closeSync();
  }
}

// libc, through dart:ffi (no package). Android's bionic and glibc export the
// 64-bit offset variants (needed on 32-bit ARM); Apple's libc has only the
// plain names, which are 64-bit there.
typedef _PreadC = IntPtr Function(Int32, Pointer<Uint8>, IntPtr, Int64);
typedef _PreadD = int Function(int, Pointer<Uint8>, int, int);
typedef _LseekC = Int64 Function(Int32, Int64, Int32);
typedef _LseekD = int Function(int, int, int);
typedef _CloseC = Int32 Function(Int32);
typedef _CloseD = int Function(int);
typedef _MallocC = Pointer<Uint8> Function(IntPtr);
typedef _MallocD = Pointer<Uint8> Function(int);
typedef _FreeC = Void Function(Pointer<Uint8>);
typedef _FreeD = void Function(Pointer<Uint8>);
typedef _ErrnoC = Pointer<Int32> Function();

final class _Libc {
  _Libc._(DynamicLibrary lib)
    : pread = lib.lookupFunction<_PreadC, _PreadD>(_name(lib, 'pread')),
      lseek = lib.lookupFunction<_LseekC, _LseekD>(_name(lib, 'lseek')),
      close = lib.lookupFunction<_CloseC, _CloseD>('close'),
      malloc = lib.lookupFunction<_MallocC, _MallocD>('malloc'),
      free = lib.lookupFunction<_FreeC, _FreeD>('free'),
      _errno = _errnoFn(lib);

  static final instance = _Libc._(DynamicLibrary.process());

  static String _name(DynamicLibrary lib, String base) =>
      lib.providesSymbol('${base}64') ? '${base}64' : base;

  // bionic: __errno, glibc: __errno_location, Apple: __error.
  static _ErrnoC? _errnoFn(DynamicLibrary lib) {
    for (final name in ['__errno', '__errno_location', '__error']) {
      if (lib.providesSymbol(name)) {
        return lib.lookupFunction<_ErrnoC, _ErrnoC>(name);
      }
    }
    return null;
  }

  final _PreadD pread;
  final _LseekD lseek;
  final _CloseD close;
  final _MallocD malloc;
  final _FreeD free;
  final _ErrnoC? _errno;

  /// The calling thread's errno, best effort (0 if unknown). Read it straight
  /// after the failing call.
  int get errno {
    final f = _errno;
    return f == null ? 0 : f().value;
  }
}

const _seekEnd = 2;
const _eintr = 4; // the same on Linux, Android and Darwin
const _maxEintrRetries = 100;

/// A [StorageReader] over a file descriptor the reader owns (Android: a
/// detached SAF descriptor from `openFd`). Uses `pread`, so reads do not move
/// a shared offset and no platform channel is crossed per read.
///
/// Throws [FileSystemException] if the descriptor is not seekable (some cloud
/// providers hand out a pipe); the descriptor is closed in that case.
final class FdStorageReader implements StorageReader {
  factory FdStorageReader(int fd) {
    final libc = _Libc.instance;
    final end = libc.lseek(fd, 0, _seekEnd);
    if (end < 0) {
      final errno = libc.errno;
      libc.close(fd);
      throw FileSystemException(
        'descriptor is not seekable',
        '',
        OSError('lseek', errno),
      );
    }
    return FdStorageReader._(fd, end);
  }

  FdStorageReader._(this._fd, this.length);

  int _fd;

  @override
  final int length;

  @override
  Uint8List readAt(int offset, int count) {
    if (_fd < 0) throw StateError('reader is closed');
    if (offset < 0 || count < 0) throw RangeError('negative offset or count');
    if (offset >= length || count == 0) return Uint8List(0);
    final want = count < length - offset ? count : length - offset;
    final libc = _Libc.instance;
    final buf = libc.malloc(want);
    if (buf == nullptr) throw const FileSystemException('out of memory');
    try {
      var got = 0;
      var interrupted = 0;
      while (got < want) {
        final n = libc.pread(_fd, buf + got, want - got, offset + got);
        if (n < 0) {
          final errno = libc.errno;
          // Interrupted by a signal: retry, but not forever.
          if (errno == _eintr && ++interrupted < _maxEintrRetries) continue;
          throw FileSystemException('read failed', '', OSError('pread', errno));
        }
        if (n == 0) break;
        got += n;
      }
      return Uint8List.fromList(buf.asTypedList(got));
    } finally {
      libc.free(buf);
    }
  }

  @override
  void close() {
    if (_fd < 0) return;
    _Libc.instance.close(_fd);
    _fd = -1;
  }
}

/// A file opened for reading but not yet wrapped in a [StorageReader]. It is
/// plain data, so it can be sent to another isolate, which calls [open] there
/// (the platform call that produced it happened on the root isolate).
///
/// Ownership: a handle is used exactly once, by [open] (the reader then owns
/// the file) or by [discard]; a second use of the same object throws. Sending
/// a handle to another isolate moves that duty with it: the sender must not
/// use its copy unless the receiver is known never to have run (for example,
/// the isolate failed to start).
sealed class ReaderHandle {
  ReaderHandle();

  bool _used = false;

  /// Whether [open] or [discard] has been called on this object.
  bool get isUsed => _used;

  void _use() {
    if (_used) throw StateError('reader handle already used');
    _used = true;
  }

  /// Builds the reader.
  StorageReader open();

  /// Releases the file without reading it.
  void discard();
}

/// A `file://` URI, read with `dart:io`.
final class FileReaderHandle extends ReaderHandle {
  FileReaderHandle(this.uri);

  final String uri;

  @override
  StorageReader open() {
    _use();
    return FileStorageReader.open(Uri.parse(uri));
  }

  @override
  void discard() => _use();
}

/// A detached file descriptor (Android).
final class FdReaderHandle extends ReaderHandle {
  FdReaderHandle(this.fd);

  final int fd;

  @override
  StorageReader open() {
    _use();
    return FdStorageReader(fd);
  }

  @override
  void discard() {
    _use();
    _Libc.instance.close(fd);
  }
}
