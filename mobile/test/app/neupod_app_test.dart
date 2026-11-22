// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter/widgets.dart' show Size;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/app/providers.dart';

import 'package:neupod/ui/device/device_view.dart';

import '../fakes/fake_player.dart';
import '../support/manifest_library_view.dart';

void main() {
  testWidgets('the app starts as the device, showing Home', (tester) async {
    // A phone, as the design requires (the default test view is 800x600).
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(393, 852);
    addTearDown(tester.view.reset);
    final library = ManifestLibraryView.parse(
      File('test/support/gate_library/manifest.json').readAsStringSync(),
    );
    await tester.pumpWidget(
      ProviderScope(
        // Retry off in tests (decision 0006), so a failing provider fails at
        // once.
        retry: (_, _) => null,
        overrides: [
          playerProvider.overrideWithValue(FakePlayer()),
          libraryViewProvider.overrideWith((ref) => library),
        ],
        child: const NeuPodApp(),
      ),
    );
    await tester.pump();
    await tester.pump();

    expect(find.byType(DeviceView), findsOneWidget);
    expect(find.text('Music'), findsOneWidget);
    expect(find.text('Shuffle Songs'), findsOneWidget);
    // Nothing is loaded, so there is no Now Playing item.
    expect(find.text('Now Playing'), findsNothing);
  });
}
