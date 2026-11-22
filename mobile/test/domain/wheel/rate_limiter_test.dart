// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/rate_limiter.dart';

void main() {
  test('at most 30 a second, the rest dropped', () {
    final l = RateLimiter();
    var allowed = 0;
    for (var us = 0; us < 1000000; us += 1000) {
      if (l.allow(us)) allowed++;
    }
    expect(allowed, 30);
  });

  test('events 1/30 s apart all pass', () {
    final l = RateLimiter();
    expect(l.allow(0), isTrue);
    expect(l.allow(33333), isFalse);
    expect(l.allow(33334), isTrue);
  });

  test('a clock going back does not block for ever', () {
    final l = RateLimiter();
    l.allow(1000000);
    expect(l.allow(10), isTrue);
    l.reset();
    expect(l.allow(10), isTrue);
  });
}
