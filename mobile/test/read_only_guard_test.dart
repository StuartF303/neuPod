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
// - Dart: all of lib/ (lib/app and lib/ui can import dart:io too) and
//   packages/*/lib;
// - Kotlin: packages/*/android/src/main;
// - Swift: packages/*/ios;
//
// (Pigeon-generated `*.g.*` files are skipped: they hold only message codecs.
// Test code is not scanned: tests build their own temp folders.) Comments are
// removed first, then the whole file (not line by line, so a call split over
// lines is seen) is matched against the file-writing APIs of its language:
//
// - Dart: `File.write*`, `openWrite`, `delete`, `rename`, `copy`, `create`,
//   `setLastModified`, write modes. The method names are matched as bare
//   words, so a tear-off (`final d = file.delete;`) is caught as well as a
//   call.
// - Kotlin: `ContentResolver.openOutputStream` / `delete` / `insert` /
//   `update`; the bare names `deleteDocument`, `renameDocument`,
//   `moveDocument`, `copyDocument`, `createDocument` (so a static import or a
//   method reference `DocumentsContract::deleteDocument` is caught);
//   DocumentFile `createFile` / `createDirectory`; write modes of
//   `openFileDescriptor`; `File.delete` / `renameTo` / `mkdirs`; the
//   write-permission flags.
// - Swift: `FileManager` remove/move/copy/create/replace, `FileHandle(
//   forWriting...)`, `write(to:)`, and `NSFileCoordinator` `writingItemAt` /
//   `writingItemURL` / write options (`readingItemAt` alone passes).
//
// A match fails the test unless it is in [_allowed]. The allow-list is per
// occurrence: an entry names a file and the exact (trimmed) source line, and
// is used up by one match of that line, so a second call that happens to look
// the same still fails. Each entry says why it is not a music location (the
// app's own database or cache). An entry that no longer matches also fails, so
// the list cannot rot. The guard is a tripwire, not a proof (code can build a
// call from strings); the runtime test and the device check are what show the
// files unchanged.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

enum _Lang { dart, kotlin, swift }

/// The write APIs, by language. Each is a regular expression on the whole
/// comment-free source of a file (`\s` crosses line breaks).
final _forbidden = <_Lang, List<RegExp>>{
  _Lang.dart: [
    RegExp(
      r'\.(writeAsBytes|writeAsString|writeAsBytesSync|writeAsStringSync|'
      r'openWrite|writeFromSync|writeByteSync|writeStringSync|truncateSync|'
      r'setLastModified|setLastModifiedSync|setLastAccessed|'
      r'setLastAccessedSync)\b',
    ),
    RegExp(r'\.(delete|deleteSync|rename|renameSync)\b'),
    RegExp(r'\.(copy|copySync|create|createSync|createTemp|createTempSync)\b'),
    RegExp(r'\bFileMode\.(write|append|writeOnly|writeOnlyAppend)\b'),
    RegExp(r'\bRandomAccessFile\b[^;]*\.(write|truncate|flush)\b'),
  ],
  _Lang.kotlin: [
    RegExp(r'\bopenOutputStream\b'),
    RegExp(r'\bopenFileDescriptor\([^)]*"[^"]*[wat][^"]*"'),
    RegExp(r'\b(contentResolver|resolver)\s*\.\s*(delete|insert|update)\b'),
    RegExp(r'\b(delete|rename|move|copy|create)Document\b'),
    RegExp(r'\.\s*(createFile|createDirectory)\b'),
    RegExp(r'\bFileOutputStream\b|\bFileWriter\b|\bBufferedWriter\b'),
    RegExp(r'\bRandomAccessFile\([^)]*"rw'),
    RegExp(
      r'\.\s*(delete|deleteRecursively|renameTo|mkdir|mkdirs|createNewFile|'
      r'writeBytes|writeText|appendText|appendBytes|outputStream|'
      r'bufferedWriter|printWriter|setLastModified)\b',
    ),
    RegExp(r'\bFiles\.(write|delete|move|copy|createFile|createDirector)'),
    RegExp(r'\bFLAG_GRANT_(WRITE|PREFIX)_URI_PERMISSION\b'),
    RegExp(r'\bMODE_(WRITE_ONLY|READ_WRITE|APPEND|TRUNCATE)\b'),
  ],
  _Lang.swift: [
    RegExp(
      r'\.(removeItem|moveItem|copyItem|createFile|createDirectory|'
      r'replaceItem|trashItem|setAttributes|createSymbolicLink|linkItem)\b',
    ),
    RegExp(r'\bFileHandle\(\s*for(Writing|Updating)'),
    RegExp(r'\.write\(\s*to\s*:'),
    RegExp(r'\bOutputStream\b'),
    RegExp(r'\bAVAudioFile\(\s*forWriting'),
    RegExp(r'\bwritingItem(At|URL)\b'),
    RegExp(r'\bfor(Replacing|Deleting|Moving|Merging)\b'),
    RegExp(r'\b(fopen|open)\([^)]*(O_WRONLY|O_RDWR|"[wa]\+?")'),
  ],
};

/// One permitted occurrence: the line [line] of [file] (relative to mobile/,
/// trimmed, compared whole) is the app's own storage, never a music location.
class _Allowed {
  const _Allowed(this.file, this.line, this.reason);
  final String file;
  final String line;
  final String reason;
}

/// The app's own database and cache directories. Nothing else.
const _allowed = <_Allowed>[
  _Allowed(
    'lib/audio/test_media.dart',
    'await file.parent.create(recursive: true);',
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
    'await (_db.delete(',
    'drift: deletes source rows of the app database, not files',
  ),
  _Allowed(
    'lib/data/scan/local_library_scanner.dart',
    'await (_db.delete(_db.tracks)..where(',
    'drift: deletes track rows of the app database, not files',
  ),
  // The wheel text entry's `delete` key (backspace): an enum value, no file.
  _Allowed(
    'lib/ui/screens/search/search_session.dart',
    "entry.TextChip.delete => 'del',",
    'the Search keyboard\'s backspace chip, not a file operation',
  ),
  _Allowed(
    'lib/ui/screens/search/search_screen.dart',
    '_session.chip != entry.TextChip.delete) {',
    'the Search keyboard\'s backspace chip, not a file operation',
  ),
  _Allowed(
    'lib/domain/text_entry/wheel_text_entry.dart',
    'TextChip.delete,',
    'the Search keyboard\'s backspace chip, not a file operation',
  ),
  _Allowed(
    'lib/domain/text_entry/wheel_text_entry.dart',
    "if (chip == TextChip.delete) return (TextKeyKind.delete, 'del');",
    'the Search keyboard\'s backspace chip, not a file operation',
  ),
  _Allowed(
    'lib/domain/text_entry/wheel_text_entry.dart',
    'case TextKeyKind.delete:',
    'the Search keyboard\'s backspace key, not a file operation',
  ),
  _Allowed(
    'packages/neupod_storage/ios/neupod_storage/Sources/neupod_storage/'
        'NeupodStoragePlugin.swift',
    'try FileManager.default.createDirectory(at: dir, '
        'withIntermediateDirectories: true)',
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

/// The `(line number, line)` of each place in [source] that uses a write API
/// for [lang]. A match that spans lines is reported at its first line.
List<(int, String)> _violations(String source, _Lang lang) {
  final text = _stripComments(source);
  final lines = text.split('\n');
  final starts = <int>{};
  for (final re in _forbidden[lang]!) {
    for (final m in re.allMatches(text)) {
      starts.add('\n'.allMatches(text.substring(0, m.start)).length);
    }
  }
  return [for (final i in starts.toList()..sort()) (i + 1, lines[i].trim())];
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

  walk('lib', '.dart', _Lang.dart);
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
    expect(sources.length, greaterThan(40));
    expect(sources.keys, contains('lib/ui/screens/browse/home_screen.dart'));

    final unused = [..._allowed]; // each entry is used up by one occurrence
    final bad = <String>[];
    sources.forEach((path, lang) {
      for (final (n, line) in _violations(
        File(path).readAsStringSync(),
        lang,
      )) {
        final i = unused.indexWhere(
          (a) => path.endsWith(a.file) && line == a.line,
        );
        if (i < 0) {
          bad.add('$path:$n: $line');
        } else {
          unused.removeAt(i);
        }
      }
    });
    expect(
      bad,
      isEmpty,
      reason:
          'A music file or folder must never be written, moved or deleted. '
          "If this is the app's own database or cache, add the exact line to "
          '_allowed with the reason.',
    );
    expect(
      unused.map((a) => '${a.file}: ${a.line}'),
      isEmpty,
      reason: 'a stale _allowed entry: remove it',
    );
  });

  group('the patterns catch what they are for', () {
    void flags(_Lang lang, String src) =>
        expect(_violations(src, lang), isNotEmpty, reason: src);
    void passes(_Lang lang, String src) =>
        expect(_violations(src, lang), isEmpty, reason: src);

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
        // Tear-offs: no parenthesis after the name.
        'final d = file.delete;\nawait d();',
        'final r = file.renameSync;',
        // Split over lines.
        'await file\n    .delete();',
        'await file.writeAsBytes(\n  bytes,\n);',
      ]) {
        flags(_Lang.dart, l);
      }
      passes(_Lang.dart, 'final b = file.readAsBytesSync();');
      passes(_Lang.dart, '// file.delete() is never called');
      passes(_Lang.dart, '/* f.writeAsBytes(b) */ final x = 1;');
      passes(_Lang.dart, 'final u = Uri.parse("file:///a");');
      passes(_Lang.dart, 'final c = track.copyWith(title: "x");');
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
        // Split over lines.
        'contentResolver\n    .delete(uri, null, null)',
        'DocumentsContract\n    .deleteDocument(cr, uri)',
        'cr.openFileDescriptor(\n    uri,\n    "rw",\n)',
        // Static import: the bare name.
        'import android.provider.DocumentsContract.deleteDocument\n'
            'deleteDocument(cr, uri)',
        'val d = deleteDocument(cr, uri)',
        'moveDocument(cr, a, b, c)',
        // Method reference.
        'val f = DocumentsContract::deleteDocument',
        'run(DocumentsContract::renameDocument)',
        // DocumentFile.
        'DocumentFile.fromTreeUri(c, u)!!.createFile("audio/flac", "x")',
        'dir.createDirectory("new")',
      ]) {
        flags(_Lang.kotlin, l);
      }
      passes(_Lang.kotlin, 'cr.openFileDescriptor(uri, "r")');
      passes(_Lang.kotlin, 'walks.remove(walkId)');
      passes(_Lang.kotlin, '// contentResolver.delete(uri) is never used');
      passes(_Lang.kotlin, 'DocumentsContract.buildDocumentUriUsingTree(a, b)');
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
        // Coordinated writes, with and without a deleting option.
        'coordinator.coordinate(writingItemAt: url, options: [], error: &e) { _ in }',
        'c.coordinate(writingItemAt: url,\n    options: [],\n    error: &e) { _ in }',
        'c.coordinate(readingItemAt: a, options: [], writingItemAt: b, options: [], error: &e) { _, _ in }',
        'c.prepare(forReadingItemsAt: [], writingItemsAt: [], options: [])\n'
            'let w = NSFileAccessIntent.writingIntent(with: u)\n'
            'coordinate(with: [intent], queue: q) { _ in }\n'
            'let x = writingItemURL',
        'try fm.createFile(atPath: p, contents: d)',
      ]) {
        flags(_Lang.swift, l);
      }
      passes(_Lang.swift, 'let f = try AVAudioFile(forReading: url)');
      passes(_Lang.swift, 'walks.removeValue(forKey: walkId)');
      passes(_Lang.swift, '/// Nothing here writes, moves or deletes a file.');
      passes(
        _Lang.swift,
        'coordinator.coordinate(readingItemAt: url, options: [], error: &e) { _ in }',
      );
    });

    test('an allow-list entry covers one occurrence, not every like line', () {
      const src = 'await (_db.delete(\nawait (_db.delete(\n';
      expect(_violations(src, _Lang.dart), hasLength(2));
    });
  });
}
