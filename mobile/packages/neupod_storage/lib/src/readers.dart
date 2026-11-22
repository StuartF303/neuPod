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
    return FileStorageReader._(file, file.lengthSync());
  }

  final RandomAccessFile _file;

  @override
  final int length;

  @override
  Uint8List readAt(int offset, int count) {
    if (offset < 0 || count < 0) throw RangeError('negative offset or count');
    if (offset >= length || count == 0) return Uint8List(0);
    _file.setPositionSync(offset);
    return _file.readSync(count);
  }

  @override
  void close() => _file.closeSync();
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

final class _Libc {
  _Libc._(DynamicLibrary lib)
    : pread = lib.lookupFunction<_PreadC, _PreadD>(_name(lib, 'pread')),
      lseek = lib.lookupFunction<_LseekC, _LseekD>(_name(lib, 'lseek')),
      close = lib.lookupFunction<_CloseC, _CloseD>('close'),
      malloc = lib.lookupFunction<_MallocC, _MallocD>('malloc'),
      free = lib.lookupFunction<_FreeC, _FreeD>('free');

  static final instance = _Libc._(DynamicLibrary.process());

  static String _name(DynamicLibrary lib, String base) =>
      lib.providesSymbol('${base}64') ? '${base}64' : base;

  final _PreadD pread;
  final _LseekD lseek;
  final _CloseD close;
  final _MallocD malloc;
  final _FreeD free;
}

const _seekEnd = 2;

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
      libc.close(fd);
      throw const FileSystemException('descriptor is not seekable');
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
      while (got < want) {
        final n = libc.pread(_fd, buf + got, want - got, offset + got);
        if (n < 0) throw const FileSystemException('read failed');
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
sealed class ReaderHandle {
  const ReaderHandle();

  /// Builds the reader. Call once; the reader then owns the file.
  StorageReader open();

  /// Releases the file without reading it.
  void discard();
}

/// A `file://` URI, read with `dart:io`.
final class FileReaderHandle extends ReaderHandle {
  const FileReaderHandle(this.uri);

  final String uri;

  @override
  StorageReader open() => FileStorageReader.open(Uri.parse(uri));

  @override
  void discard() {}
}

/// A detached file descriptor (Android).
final class FdReaderHandle extends ReaderHandle {
  const FdReaderHandle(this.fd);

  final int fd;

  @override
  StorageReader open() => FdStorageReader(fd);

  @override
  void discard() => _Libc.instance.close(fd);
}
