// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library/library_summary.dart';

void main() {
  group('LibrarySummary', () {
    test('an empty library says "No songs"', () {
      const summary = LibrarySummary(trackCount: 0);
      expect(summary.isEmpty, isTrue);
      expect(summary.songCountLabel, 'No songs');
    });

    test('one track is singular', () {
      expect(const LibrarySummary(trackCount: 1).songCountLabel, '1 song');
    });

    test('more tracks are plural', () {
      const summary = LibrarySummary(trackCount: 20000);
      expect(summary.isEmpty, isFalse);
      expect(summary.songCountLabel, '20000 songs');
    });

    test('equality is by value', () {
      expect(
        const LibrarySummary(trackCount: 3),
        const LibrarySummary(trackCount: 3),
      );
      expect(
        const LibrarySummary(trackCount: 3),
        isNot(const LibrarySummary(trackCount: 4)),
      );
    });

    test('a negative count is rejected', () {
      expect(
        () => LibrarySummary(trackCount: int.parse('-1')),
        throwsA(isA<AssertionError>()),
      );
    });
  });
}
