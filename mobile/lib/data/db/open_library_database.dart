// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:drift/drift.dart';
import 'package:drift/native.dart';
import 'package:neupod/data/db/library_database.dart';

/// Opens the library database lazily, on first use, in a background isolate
/// (decision 0005), as `library.sqlite` in the directory [directory]
/// returns (the app's private Application Support / files directory).
LibraryDatabase openLibraryDatabase(Future<String> Function() directory) =>
    LibraryDatabase(
      LazyDatabase(() async {
        final dir = await directory();
        return NativeDatabase.createInBackground(File('$dir/library.sqlite'));
      }),
    );
