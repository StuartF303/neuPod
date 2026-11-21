// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_summary.dart';
import 'package:neupod/ui/features/home/home_screen.dart';

void main() {
  testWidgets('Home shows the library size from an overridden provider', (
    tester,
  ) async {
    await tester.pumpWidget(
      ProviderScope(
        retry: (_, _) => null,
        overrides: [
          librarySummaryProvider.overrideWithValue(
            const LibrarySummary(trackCount: 3),
          ),
        ],
        child: const MaterialApp(home: HomeScreen()),
      ),
    );

    expect(find.text('3 songs'), findsOneWidget);
  });
}
