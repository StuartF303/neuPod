// SPDX-License-Identifier: MPL-2.0

// main() must set up the binding before it builds any override: the Android
// scan service registers its channel handler in its constructor, and with no
// binding the app died in main() on Android (Pixel 7, after #146). Tests and
// integration tests always set up a binding first, so they cannot see this at
// run time; this checks the order in the source instead.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('main() sets up the binding before building the overrides', () {
    final source = File('lib/main.dart').readAsStringSync();
    final body = source.substring(source.indexOf('void main()'));

    final binding = body.indexOf('WidgetsFlutterBinding.ensureInitialized()');
    final overrides = body.indexOf('Overrides()');
    final run = body.indexOf('runApp(');

    expect(binding, isNonNegative, reason: 'main() must set up the binding');
    expect(overrides, isNonNegative);
    expect(binding, lessThan(overrides));
    expect(binding, lessThan(run));
  });
}
