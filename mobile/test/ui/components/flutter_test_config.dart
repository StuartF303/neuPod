// SPDX-License-Identifier: MPL-2.0

// Golden set-up for the component tests (applies to this folder and below).
//
// Determinism:
// - Montserrat Medium is loaded from the bundled asset, so text renders in the
//   real face instead of the test font; icons are vector paths, no font.
// - Every golden is rendered at device pixel ratio 1 on a fixed surface size,
//   with shadows blurred as on a phone (goldenWithShadows in golden_harness).
//
// Platform: text rasterisation differs between operating systems, and between
// macOS releases too (macOS 27 moved about 0.1 % of the pixels of every golden
// made on macOS 26, all on glyph edges). So the PNGs are made and compared on
// one machine only: the `goldens` job of .github/workflows/app.yml, on its
// pinned GitHub-hosted macOS image.
//
// - NEUPOD_GOLDENS=compare: pixels are compared. The `goldens` job sets it. On
//   any other machine the result may differ from CI's, and a note says so.
// - Otherwise (the Linux `check` job, a plain local `flutter test`): the
//   widgets are still built, laid out and painted, so overflow and exceptions
//   still fail the test, but the pixels are not compared.
// - `--update-goldens` writes PNGs only on GitHub Actions with
//   NEUPOD_GOLDENS=compare. Elsewhere it writes nothing. To regenerate, run the
//   app workflow by hand with `update_goldens` (docs/app/ENVIRONMENT.md).

import 'dart:async';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

/// Whether golden pixels are compared on this machine.
bool get goldensCompared => Platform.environment['NEUPOD_GOLDENS'] == 'compare';

/// Whether this runs on GitHub Actions (the `goldens` job's image).
bool get _onCi => Platform.environment['GITHUB_ACTIONS'] == 'true';

Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  TestWidgetsFlutterBinding.ensureInitialized();
  final bytes = File('assets/fonts/Montserrat-Medium.ttf').readAsBytesSync();
  final loader = FontLoader('Montserrat')
    ..addFont(Future.value(ByteData.sublistView(bytes)));
  await loader.load();

  if (goldenFileComparator is LocalFileComparator) {
    final local = goldenFileComparator as LocalFileComparator;
    final testFile = local.basedir.resolve('x.dart');
    if (!goldensCompared) {
      if (Platform.isMacOS && !_onCi) {
        // ignore: avoid_print
        print(
          'neupod: golden pixels are not compared locally. Set '
          'NEUPOD_GOLDENS=compare to compare them; this Mac may differ from '
          'the CI macOS image the PNGs are made on.',
        );
      }
      goldenFileComparator = _RenderOnlyComparator(testFile);
    } else if (!_onCi) {
      // ignore: avoid_print
      print(
        'neupod: comparing goldens on this machine. The PNGs are made on the '
        "CI macOS image (app.yml 'goldens' job), and text may rasterise "
        'slightly differently here: failures of about 0.1 % on glyph edges '
        'are this machine, not the app.',
      );
      goldenFileComparator = _LocalCompareComparator(testFile);
    }
  }
  await testMain();
}

/// Accepts every image without comparing, and never writes one.
class _RenderOnlyComparator extends LocalFileComparator {
  _RenderOnlyComparator(super.testFile);

  @override
  Future<bool> compare(Uint8List imageBytes, Uri golden) async => true;

  @override
  Future<void> update(Uri golden, Uint8List imageBytes) async {
    // ignore: avoid_print
    print('neupod: not writing $golden: goldens are made in CI');
  }
}

/// Compares as usual, but never writes: the PNGs come from the CI image.
class _LocalCompareComparator extends LocalFileComparator {
  _LocalCompareComparator(super.testFile);

  @override
  Future<void> update(Uri golden, Uint8List imageBytes) async {
    // ignore: avoid_print
    print(
      'neupod: not writing $golden: goldens are made in CI (run the app '
      'workflow with update_goldens, docs/app/ENVIRONMENT.md)',
    );
  }
}
