// SPDX-License-Identifier: MPL-2.0

// Golden comparison for the device view, with a small tolerance.
//
// The goldens are made on macOS and checked on the Linux CI runner. The
// device goldens host a text-free test pattern in the screen, but they still
// draw text: MENU on the wheel (TextPainter) and the wordmark, in the
// FlutterTest font. Small text and anti-aliased edges (the wheel's circles,
// gradients, shadows) can differ by a few pixels between the two hosts.
//
// A golden passes when at most [_tolerance] (0.5 %) of its pixels differ.
// That is acceptable because the goldens are not the guard on positions:
// test/ui/device/device_geometry_test.dart pins every position to render.py
// within 1e-9 pt, and device_view_test.dart checks the rendered screen and
// wheel rects. The goldens catch drawing changes (colours, shapes, states),
// which move far more than 0.5 % of the image.
// Regenerate with `flutter test --update-goldens test/ui/device/goldens`.

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';

const double _tolerance = 0.005; // 0.5 % of the pixels

Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  final base = goldenFileComparator;
  if (base is LocalFileComparator) {
    goldenFileComparator = _TolerantComparator(
      Uri.parse('${base.basedir}placeholder.dart'),
    );
  }
  await testMain();
}

class _TolerantComparator extends LocalFileComparator {
  _TolerantComparator(super.testFile);

  @override
  Future<bool> compare(Uint8List imageBytes, Uri golden) async {
    final result = await GoldenFileComparator.compareLists(
      imageBytes,
      await getGoldenBytes(golden),
    );
    if (result.passed || result.diffPercent <= _tolerance) {
      result.dispose();
      return true;
    }
    final error = await generateFailureOutput(result, golden, basedir);
    result.dispose();
    throw FlutterError(error);
  }
}
