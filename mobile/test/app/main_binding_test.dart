// SPDX-License-Identifier: MPL-2.0

// main() must set up the binding before anything else: the Android scan
// service registers its channel handler in its constructor, and with no
// binding the app died in main() on Android (Pixel 7, after #146). Tests and
// integration tests always set up a binding first, so they cannot see this at
// run time; this checks main()'s first statement in the source instead.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test("main()'s first statement sets up the binding", () {
    final source = File('lib/main.dart').readAsStringSync();
    final code = source
        .split('\n')
        .map((line) => line.replaceFirst(RegExp(r'//.*'), ''))
        .join('\n');

    final start = code.indexOf('void main()');
    expect(start, isNonNegative, reason: 'lib/main.dart has no main()');
    final body = code.substring(code.indexOf('{', start) + 1).trimLeft();

    expect(body, startsWith('WidgetsFlutterBinding.ensureInitialized();'));
  });
}
