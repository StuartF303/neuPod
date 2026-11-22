// SPDX-License-Identifier: MPL-2.0

import 'dart:async';
import 'dart:io';

import 'package:flutter/widgets.dart' show AppLifecycleState, Size;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/neupod_app.dart';
import 'package:neupod/app/providers.dart';

import 'package:neupod/ui/device/device_view.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';

import 'package:neupod/domain/library/library_folder.dart';

import '../fakes/fake_folder_access.dart';
import '../fakes/fake_library_scanner.dart';
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
          folderAccessProvider.overrideWithValue(FakeFolderAccess()),
          libraryScannerProvider.overrideWithValue(FakeLibraryScanner()),
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

  for (final (what, folders, scans) in [
    ('a folder', [_documents], 1),
    ('no folder', <LibraryFolder>[], 0),
  ]) {
    testWidgets('launching with $what starts $scans scan(s)', (tester) async {
      final scanner = FakeLibraryScanner();
      await tester.pumpWidget(
        ProviderScope(
          retry: (_, _) => null,
          overrides: [
            playerProvider.overrideWithValue(FakePlayer()),
            libraryViewProvider.overrideWith(
              (ref) => ManifestLibraryView.parse(
                File('test/support/gate_library/manifest.json')
                    .readAsStringSync(),
              ),
            ),
            folderAccessProvider.overrideWithValue(FakeFolderAccess(folders)),
            libraryScannerProvider.overrideWithValue(scanner),
          ],
          child: const NeuPodApp(),
        ),
      );
      await tester.pump();
      await tester.pump();
      expect(scanner.started, scans);
    });
  }
  Future<(FakeLibraryScanner, ProviderContainer)> launch(
    WidgetTester tester,
  ) async {
    final scanner = FakeLibraryScanner();
    await tester.pumpWidget(
      ProviderScope(
        retry: (_, _) => null,
        overrides: [
          playerProvider.overrideWithValue(FakePlayer()),
          libraryViewProvider.overrideWith(
            (ref) => ManifestLibraryView.parse(
              File('test/support/gate_library/manifest.json')
                  .readAsStringSync(),
            ),
          ),
          folderAccessProvider.overrideWithValue(
            FakeFolderAccess([_documents]),
          ),
          libraryScannerProvider.overrideWithValue(scanner),
        ],
        child: const NeuPodApp(),
      ),
    );
    await tester.pump();
    await tester.pump();
    final container = ProviderScope.containerOf(
      tester.element(find.byType(DeviceView)),
    );
    return (scanner, container);
  }

  testWidgets('a failed scan says why in a panel, as the firmware does', (
    tester,
  ) async {
    final (scanner, container) = await launch(tester);
    scanner.current.addError(
      StateError('SqliteException(13): database or disk is full'),
    );
    await tester.pump();
    await tester.pump();
    final panel = container.read(shellProvider).panel!;
    expect(panel.title, 'Storage is full');
    expect(panel.message, contains('database or disk is full'));
  });

  testWidgets('a cancelled scan says nothing', (tester) async {
    final (scanner, container) = await launch(tester);
    // Not awaited: the test clock runs the cancel when it pumps.
    unawaited(container.read(libraryScanProvider.notifier).cancel());
    await tester.pump();
    await tester.pump();
    expect(container.read(shellProvider).panel, isNull);
    expect(scanner.cancelled, 1);
  });

  testWidgets('back in front: a scan, unless the user cancelled the last', (
    tester,
  ) async {
    final (scanner, container) = await launch(tester);
    expect(scanner.started, 1);
    await scanner.finish();
    await tester.pump();

    Future<void> backInFront() async {
      tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.inactive);
      tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
      await tester.pump();
      await tester.pump();
    }

    await backInFront();
    expect(scanner.started, 2);

    // Not awaited: the test clock runs the cancel when it pumps.
    unawaited(container.read(libraryScanProvider.notifier).cancel());
    await tester.pump();
    await backInFront();
    expect(scanner.started, 2, reason: 'the user cancelled it');
  });
}

/// The iOS Documents folder: always there, so iOS always scans at launch.
const _documents = LibraryFolder(
  id: 'documents',
  name: 'neuPod',
  kind: FolderKind.appDocuments,
  available: true,
);
