// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:material_ui/material_ui.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/ui/features/library_setup/library_setup_screen.dart';

import '../../../fakes/fake_folder_access.dart';

const documents = LibraryFolder(
  id: 'documents',
  name: 'neuPod',
  kind: FolderKind.appDocuments,
  available: true,
);
const music = LibraryFolder(
  id: 'content://tree/Music',
  name: 'Music',
  kind: FolderKind.pickedFolder,
  available: true,
);
const drive = LibraryFolder(
  id: 'bookmark-1',
  name: 'USB',
  kind: FolderKind.pickedFolder,
  available: false,
);

Future<void> pumpScreen(WidgetTester tester, FakeFolderAccess access) async {
  await tester.pumpWidget(
    ProviderScope(
      retry: (_, _) => null,
      overrides: [folderAccessProvider.overrideWithValue(access)],
      child: const MaterialApp(home: LibrarySetupScreen()),
    ),
  );
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('lists folders; Documents cannot be removed', (tester) async {
    await pumpScreen(tester, FakeFolderAccess([documents, music, drive]));

    expect(find.text('neuPod'), findsOneWidget);
    expect(find.text('On My iPhone'), findsOneWidget);
    expect(find.text('Music'), findsOneWidget);
    expect(find.text('Not available'), findsOneWidget);
    // Two picked folders, two Remove buttons; none for Documents.
    expect(find.text('Remove'), findsNWidgets(2));
  });

  testWidgets('empty state', (tester) async {
    await pumpScreen(tester, FakeFolderAccess());
    expect(find.text('No folders yet'), findsOneWidget);
  });

  testWidgets('Add folder adds the picked folder', (tester) async {
    final access = FakeFolderAccess()..nextPick = music;
    await pumpScreen(tester, access);

    await tester.tap(find.text('Add folder'));
    await tester.pumpAndSettle();

    expect(find.text('Music'), findsOneWidget);
  });

  testWidgets('a cancelled pick changes nothing', (tester) async {
    await pumpScreen(tester, FakeFolderAccess([music]));
    await tester.tap(find.text('Add folder'));
    await tester.pumpAndSettle();
    expect(find.byType(ListTile), findsOneWidget);
  });

  testWidgets('a picker failure is shown, not thrown', (tester) async {
    final access = FakeFolderAccess()
      ..pickError = const FolderAccessException('busy');
    await pumpScreen(tester, access);

    await tester.tap(find.text('Add folder'));
    await tester.pumpAndSettle();

    expect(find.text('Could not add the folder (busy)'), findsOneWidget);
  });

  testWidgets('Remove releases the folder', (tester) async {
    final access = FakeFolderAccess([music]);
    await pumpScreen(tester, access);

    await tester.tap(find.text('Remove'));
    await tester.pumpAndSettle();

    expect(access.removed, [music.id]);
    expect(find.text('No folders yet'), findsOneWidget);
  });

  test('the Android hint names the folders Android refuses', () {
    final hint = LibrarySetupScreen.hintFor(TargetPlatform.android);
    expect(hint, contains('Download'));
    expect(hint, contains('Music'));
    expect(
      LibrarySetupScreen.hintFor(TargetPlatform.iOS),
      contains('On My iPhone > neuPod'),
    );
  });
}
