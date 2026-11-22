// SPDX-License-Identifier: MPL-2.0

// Golden set-up for these tests: the components' (Montserrat loaded, goldens
// compared on macOS only; see ../components/flutter_test_config.dart).

import 'dart:async';

import '../components/flutter_test_config.dart' as components;

Future<void> testExecutable(FutureOr<void> Function() testMain) =>
    components.testExecutable(testMain);
