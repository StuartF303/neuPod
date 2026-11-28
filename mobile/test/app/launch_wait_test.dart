// SPDX-License-Identifier: MPL-2.0

// The first frame waits for the saved settings, but never for long (a stalled
// read must not hold the app on its splash screen). main() must use the
// bounded wait, not a bare Future.wait: this was lost once in a merge.

import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/launch_wait.dart';

void main() {
  test('returns when every read has completed', () async {
    var done = false;
    await awaitLaunchReads([
      Future<void>.delayed(const Duration(milliseconds: 5)),
      Future<void>.value(),
    ]).then((_) => done = true);
    expect(done, isTrue);
  });

  test('a read that never completes is given up on after the limit', () async {
    final never = Completer<void>();
    final watch = Stopwatch()..start();
    await awaitLaunchReads([
      never.future,
      Future<void>.value(),
    ], limit: const Duration(milliseconds: 50));
    expect(watch.elapsed, lessThan(const Duration(seconds: 2)));
    expect(never.isCompleted, isFalse);
  });

  test('the limit is one second', () {
    expect(launchReadLimit, const Duration(seconds: 1));
  });

  test("main() waits through awaitLaunchReads, with the stores' reads", () {
    final source = File('lib/main.dart').readAsStringSync();
    final code = source
        .split('\n')
        .map((line) => line.replaceFirst(RegExp(r'//.*'), ''))
        .join('\n');
    expect(code, contains('await awaitLaunchReads(['));
    for (final read in [
      'styleChoiceProvider.notifier).loaded',
      'customLooksProvider.notifier).loaded',
      'systemReduceTransparencyProvider.notifier).loaded',
    ]) {
      expect(code, contains(read));
    }
    expect(code, isNot(contains('Future.wait(')), reason: 'unbounded wait');
  });
}
