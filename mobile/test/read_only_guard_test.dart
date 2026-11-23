// SPDX-License-Identifier: MPL-2.0

// The static half of the read-only guarantee (README "The library": the app
// never writes, moves or deletes a music file or folder). The runtime half is
// test/data/scan/read_only_guarantee_test.dart; on a device,
// integration_test/folder_access_test.dart (run by
// scripts/app/verify-folder-access-{android,ios}.sh) reads a real picked
// folder through the native plugin.
//
// How the guard works. It reads the source of every place that can reach a
// music location:
//
// - Dart: lib/data, lib/audio and packages/*/lib;
// - Kotlin: packages/*/android/src/main;
// - Swift: packages/*/ios;
//
// (Pigeon-generated `*.g.*` files are skipped: they hold only message codecs.
// Test code is not scanned: tests build their own temp folders.) Comments are
// removed first, then each line is matched against the file-writing APIs for
// its language: `File.write*`, `openWrite`, `delete`, `rename`, `copy`,
// `create`, `setLastModified` and write modes on the Dart side;
// `ContentResolver.openOutputStream` / `delete` / `insert` / `update`,
// `DocumentsContract` delete/rename/move/copy/create, write modes of
// `openFileDescriptor`, `File.delete` / `renameTo` / `mkdirs` and the
// write-permission flags on the Kotlin side; `FileManager`
// remove/move/copy/create/replace, `FileHandle(forWriting...)`, `write(to:)`
// and `NSFileCoordinator` write options on the Swift side. A match fails the
// test unless it is in [_allowed].
//
// The allow-list is explicit and narrow: each entry names a file, the exact
// text that may appear on the line, and why it is not a music location (the
// app's own database or cache). A new write call to a music location cannot
// be added by editing the allow-list quietly: the reason is reviewed with it.
// An entry that no longer matches anything also fails, so the list cannot rot.
// The guard is a tripwire, not a proof (code can build a call from strings);
// the runtime test and the device check are what show the files unchanged.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

enum _Lang { dart, kotlin, swift }

/// The write APIs, by language. Each is a regular expression on one line of
/// comment-free source.
final _forbidden = <_Lang, List<RegExp>>{
  _Lang.dart: [
    RegExp(
      r'\.(writeAsBytes|writeAsString|writeAsBytesSync|writeAsStringSync|'
      r'openWrite|writeFromSync|writeByteSync|writeStringSync|truncateSync|'
      r'setLastModified|setLastModifiedSync|setLastAccessed|'
      r'setLastAccessedSync|lengthSync\(\)\s*=)\b',
    ),
    RegExp(r'\.(delete|deleteSync|rename|renameSync)\('),
    RegExp(r'\.(copy|copySync|create|createSync|createTemp|createTempSync)\('),
    RegExp(r'\bFileMode\.(write|append|writeOnly|writeOnlyAppend)\b'),
    RegExp(r'\b(Link|File|Directory)\.(fromRawPath)\b'),
    RegExp(r'\bRandomAccessFile\b.*\.(write|truncate|flush)'),
  ],
  _Lang.kotlin: [
    RegExp(r'\bopenOutputStream\b'),
    RegExp(r'\bopenFileDescriptor\([^)]*"[^"]*[wat][^"]*"'),
    RegExp(r'\bcontentResolver\s*\.\s*(delete|insert|update)\b'),
    RegExp(r'\bresolver\s*\.\s*(delete|insert|update)\b'),
    RegExp(
      r'\bDocumentsContract\.(deleteDocument|renameDocument|moveDocument|'
      r'copyDocument|createDocument)\b',
    ),
    RegExp(r'\bFileOutputStream\b|\bFileWriter\b|\bBufferedWriter\b'),
    RegExp(r'\bRandomAccessFile\([^)]*"rw'),
    RegExp(
      r'\.(delete|deleteRecursively|renameTo|mkdir|mkdirs|createNewFile|'
      r'writeBytes|writeText|appendText|appendBytes|outputStream|'
      r'bufferedWriter|printWriter|setLastModified)\b\s*\(',
    ),
    RegExp(r'\bFiles\.(write|delete|move|copy|createFile|createDirector)'),
    RegExp(r'\bFLAG_GRANT_(WRITE|PREFIX)_URI_PERMISSION\b'),
    RegExp(r'\bMODE_(WRITE_ONLY|READ_WRITE|APPEND|TRUNCATE)\b'),
  ],
  _Lang.swift: [
    RegExp(
      r'\.(removeItem|moveItem|copyItem|createFile|createDirectory|'
      r'replaceItem|trashItem|setAttributes|createSymbolicLink|linkItem)\(',
    ),
    RegExp(r'\bFileHandle\(\s*for(Writing|Updating)'),
    RegExp(r'\.write\(\s*to\s*:'),
    RegExp(r'\bOutputStream\b'),
    RegExp(r'\bAVAudioFile\(\s*forWriting'),
    RegExp(r'\bfor(Replacing|Deleting|Moving|Merging)\b'),
    RegExp(r'\b(fopen|open)\([^)]*(O_WRONLY|O_RDWR|"[wa]\+?")'),
  ],
};

/// One permitted match: a line of [file] (relative to mobile/) containing
/// [text] is the app's own storage, never a music location.
class _Allowed {
  const _Allowed(this.file, this.text, this.reason);
  final String file;
  final String text;
  final String reason;
}

/// The app's own database and cache directories. Nothing else.
const _allowed = <_Allowed>[
  _Allowed(
    'lib/audio/test_media.dart',
    'await file.parent.create(recursive: true)',
    'test-tone fixtures copied into the temp/cache directory '
        '(neupod_test_media), never a music folder',
  ),
  _Allowed(
    'lib/audio/test_media.dart',
    'await file.writeAsBytes(',
    'the same app-owned fixture copy in the cache directory',
  ),
  _Allowed(
    'lib/data/scan/local_library_scanner.dart',
    '_db.delete(',
    'drift: deletes rows of the app database, not files',
  ),
  _Allowed(
    'packages/neupod_storage/ios/neupod_storage/Sources/neupod_storage/'
        'NeupodStoragePlugin.swift',
    'createDirectory(at: dir, withIntermediateDirectories: true)',
    'Application Support/library, the app database folder (decision 0005)',
  ),
  _Allowed(
    'packages/neupod_storage/android/src/main/kotlin/net/stuartfraser/'
        'neupod_storage/NeupodStoragePlugin.kt',
    'if (permission.isWritePermission) flags = flags or '
        'Intent.FLAG_GRANT_WRITE_URI_PERMISSION',
    'removeRoot RELEASES a write grant the user might hold; the picker '
        'asks only for read (takePersistableUriPermission uses READ)',
  ),
];

final _lineComment = RegExp(r'(?<!:)//.*$', multiLine: true);
final _blockComment = RegExp(r'/\*.*?\*/', dotAll: true);

/// [src] without comments, keeping every line break so line numbers hold.
String _stripComments(String src) => src
    .replaceAllMapped(
      _blockComment,
      (m) => '\n' * '\n'.allMatches(m[0]!).length,
    )
    .replaceAll(_lineComment, '');

/// The `(line number, line)` pairs of [source] that use a write API for
/// [lang].
List<(int, String)> _violations(String source, _Lang lang) {
  final hits = <(int, String)>[];
  final lines = _stripComments(source).split('\n');
  for (var i = 0; i < lines.length; i++) {
    if (_forbidden[lang]!.any((re) => re.hasMatch(lines[i]))) {
      hits.add((i + 1, lines[i].trim()));
    }
  }
  return hits;
}

bool _generated(String path) => RegExp(r'\.g\.(dart|kt|swift)$').hasMatch(path);

/// Every source file the guard reads, with its language, by path from mobile/.
Map<String, _Lang> _sources() {
  final out = <String, _Lang>{};
  void walk(String dir, String ext, _Lang lang) {
    final d = Directory(dir);
    if (!d.existsSync()) return;
    for (final f in d.listSync(recursive: true, followLinks: false)) {
      if (f is! File || !f.path.endsWith(ext) || _generated(f.path)) continue;
      if (f.path.contains('/.build/') || f.path.contains('/Pods/')) continue;
      out[f.path] = lang;
    }
  }

  walk('lib/data', '.dart', _Lang.dart);
  walk('lib/audio', '.dart', _Lang.dart);
  for (final p in Directory('packages').listSync().whereType<Directory>()) {
    walk('${p.path}/lib', '.dart', _Lang.dart);
    walk('${p.path}/android/src/main', '.kt', _Lang.kotlin);
    walk('${p.path}/ios', '.swift', _Lang.swift);
  }
  return out;
}

void main() {
  test('no code that reaches a music location writes, moves or deletes', () {
    final sources = _sources();
    // The guard must not pass by finding nothing to read.
    expect(sources.values.toSet(), _Lang.values.toSet());
    expect(sources.length, greaterThan(15));

    final used = <_Allowed>{};
    final bad = <String>[];
    sources.forEach((path, lang) {
      for (final (n, line) in _violations(
        File(path).readAsStringSync(),
        lang,
      )) {
        final ok = _allowed.where(
          (a) => path.endsWith(a.file) && line.contains(a.text),
        );
        if (ok.isEmpty) {
          bad.add('$path:$n: $line');
        } else {
          used.addAll(ok);
        }
      }
    });
    expect(
      bad,
      isEmpty,
      reason:
          'A music file or folder must never be written, moved or deleted. '
          'If this is the app\'s own database or cache, add it to _allowed '
          'with the reason.',
    );
    expect(
      _allowed
          .where((a) => !used.contains(a))
          .map((a) => '${a.file}: ${a.text}'),
      isEmpty,
      reason: 'a stale _allowed entry: remove it',
    );
  });

  group('the patterns catch what they are for', () {
    void flags(_Lang lang, String line) =>
        expect(_violations(line, lang), isNotEmpty, reason: line);
    void passes(_Lang lang, String line) =>
        expect(_violations(line, lang), isEmpty, reason: line);

    test('Dart', () {
      for (final l in [
        'await File(p).writeAsBytes(b);',
        'File(p).writeAsStringSync("x");',
        'final s = file.openWrite();',
        'await file.delete();',
        'dir.deleteSync(recursive: true);',
        'await f.rename(to);',
        'f.setLastModifiedSync(t);',
        'f.openSync(mode: FileMode.write);',
        'await Directory(p).create();',
      ]) {
        flags(_Lang.dart, l);
      }
      passes(_Lang.dart, 'final b = file.readAsBytesSync();');
      passes(_Lang.dart, '// file.delete() is never called');
      passes(_Lang.dart, '/* f.writeAsBytes(b) */ final x = 1;');
      passes(_Lang.dart, 'final u = Uri.parse("file:///a");');
    });

    test('Kotlin', () {
      for (final l in [
        'contentResolver.openOutputStream(uri)',
        'resolver.delete(uri, null, null)',
        'DocumentsContract.deleteDocument(r, uri)',
        'DocumentsContract.renameDocument(r, uri, "x")',
        'cr.openFileDescriptor(uri, "rw")',
        'File(p).delete()',
        'f.renameTo(g)',
        'FileOutputStream(fd)',
        'Intent.FLAG_GRANT_WRITE_URI_PERMISSION',
      ]) {
        flags(_Lang.kotlin, l);
      }
      passes(_Lang.kotlin, 'cr.openFileDescriptor(uri, "r")');
      passes(_Lang.kotlin, 'walks.remove(walkId)');
      passes(_Lang.kotlin, '// contentResolver.delete(uri) is never used');
    });

    test('Swift', () {
      for (final l in [
        'try FileManager.default.removeItem(at: url)',
        'try fm.moveItem(at: a, to: b)',
        'try fm.copyItem(at: a, to: b)',
        'try data.write(to: url)',
        'let h = try FileHandle(forWritingTo: url)',
        'coordinator.coordinate(writingItemAt: u, options: .forDeleting, error: nil) {}',
        'OutputStream(url: u, append: false)',
      ]) {
        flags(_Lang.swift, l);
      }
      passes(_Lang.swift, 'let f = try AVAudioFile(forReading: url)');
      passes(_Lang.swift, 'walks.removeValue(forKey: walkId)');
      passes(_Lang.swift, '/// Nothing here writes, moves or deletes a file.');
    });
  });
}
