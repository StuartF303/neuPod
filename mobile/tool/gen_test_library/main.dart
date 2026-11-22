// SPDX-License-Identifier: MPL-2.0

// Generates the test library. From mobile/:
//
//   dart run tool/gen_test_library/main.dart --out build/test_library
//   dart run tool/gen_test_library/main.dart --out build/scale_library --scale 20000
//
// Options:
//   --out DIR       where to write (required). Must be new, empty, or an
//                   earlier output of this tool (it holds the marker file).
//   --scale N       the scale library of N tracks instead of the curated cases.
//   --assets DIR    the repository's tests/assets (found automatically).
//
// Never commit the output; mobile/build/ is ignored.

import 'dart:io';

import 'gen_test_library.dart';

void main(List<String> args) {
  String? out;
  int? scale;
  String? assets;
  for (var i = 0; i < args.length; i++) {
    String value() {
      if (i + 1 >= args.length) _usage('${args[i]} needs a value');
      return args[++i];
    }

    switch (args[i]) {
      case '--out':
        out = value();
      case '--scale':
        scale = int.tryParse(value());
        if (scale == null || scale <= 0) _usage('--scale needs a count > 0');
      case '--assets':
        assets = value();
      case '-h' || '--help':
        _usage(null);
      default:
        _usage('unknown option ${args[i]}');
    }
  }
  if (out == null) _usage('--out is required');

  final sw = Stopwatch()..start();
  final result = generate(
    outDir: Directory(out),
    fixtures: assets == null ? null : Fixtures(Directory(assets)),
    scaleTracks: scale,
  );
  stdout.writeln(
    'Wrote ${result.trackFiles} track files and ${result.otherFileCount} '
    'other files (${(result.bytes / 1e6).toStringAsFixed(1)} MB) to '
    '${result.library.path} in ${sw.elapsedMilliseconds} ms.',
  );
  stdout.writeln('Manifest: ${result.manifest.path}');
}

Never _usage(String? error) {
  if (error != null) stderr.writeln('gen_test_library: $error');
  stderr.writeln(
    'usage: dart run tool/gen_test_library/main.dart --out DIR '
    '[--scale N] [--assets DIR]',
  );
  exit(error == null ? 0 : 64);
}
