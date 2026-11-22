// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/library/library_ports.dart';

/// A [LibraryScanner] the test drives: each [scan] is a new stream the test
/// feeds through [current] ([say], [finish]).
class FakeLibraryScanner implements LibraryScanner {
  /// Every scan started, in order.
  final List<StreamController<ScanProgress>> scans = [];

  /// How many scans were cancelled by their listener.
  int cancelled = 0;

  /// How many scans were started.
  int get started => scans.length;

  /// The scan started last.
  StreamController<ScanProgress> get current => scans.last;

  @override
  Stream<ScanProgress> scan() {
    // Closed by finish, or cancelled by the listener.
    // ignore: close_sinks
    final c = StreamController<ScanProgress>();
    c.onCancel = () {
      if (!c.isClosed) cancelled++;
    };
    scans.add(c);
    return c.stream;
  }

  /// The last scan reports [p].
  void say(ScanProgress p) => current.add(p);

  /// The last scan ends, done, with [found] files found and read.
  Future<void> finish({int found = 0}) async {
    current.add(ScanProgress(filesFound: found, filesRead: found, done: true));
    await current.close();
  }
}
