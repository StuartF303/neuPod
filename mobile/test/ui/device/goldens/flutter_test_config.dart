// SPDX-License-Identifier: MPL-2.0

// Golden comparison for the device view, with a small tolerance.
//
// The goldens are made on macOS and checked on the Linux CI runner. Flutter's
// test renderer draws text in the FlutterTest font on both, but anti-aliased
// edges (the wheel's circles, gradients, the shadows) can differ by a few
// pixels between the two hosts. A golden passes when at most
// [_tolerance] of its pixels differ; a layout change moves far more than that
// (shifting the wheel by 1 pt changes several percent of the image).
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
