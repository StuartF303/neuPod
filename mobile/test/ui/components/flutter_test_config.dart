// SPDX-License-Identifier: MPL-2.0

// Golden set-up for the component tests (applies to this folder and below).
//
// Determinism:
// - Montserrat Medium is loaded from the bundled asset, so text renders in the
//   real face instead of the test font; icons are vector paths, no font.
// - Every golden is rendered at device pixel ratio 1 on a fixed surface size,
//   with shadows blurred as on a phone (goldenWithShadows in golden_harness).
//
// Platform: text rasterisation differs between macOS (Core Text) and Linux
// (FreeType), so the PNGs are made and compared on macOS only, where the
// owner's machine and the macOS CI runner produce them. Elsewhere (the Linux
// `check` job) the widgets are still built, laid out and painted, so overflow
// and exceptions still fail the test, but the pixels are not compared and
// `--update-goldens` refuses to overwrite the macOS images. Set
// NEUPOD_GOLDENS=compare to compare on any platform.

import 'dart:async';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

/// Whether golden pixels are compared on this machine.
bool get goldensCompared =>
    Platform.isMacOS || Platform.environment['NEUPOD_GOLDENS'] == 'compare';

Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  TestWidgetsFlutterBinding.ensureInitialized();
  final bytes = File('assets/fonts/Montserrat-Medium.ttf').readAsBytesSync();
  final loader = FontLoader('Montserrat')
    ..addFont(Future.value(ByteData.sublistView(bytes)));
  await loader.load();

  if (!goldensCompared && goldenFileComparator is LocalFileComparator) {
    final local = goldenFileComparator as LocalFileComparator;
    goldenFileComparator = _RenderOnlyComparator(
      local.basedir.resolve('x.dart'),
    );
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
    print('neupod: not writing $golden: goldens are made on macOS');
  }
}
