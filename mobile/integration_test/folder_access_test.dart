// SPDX-License-Identifier: MPL-2.0

// Runs the real neupod_storage plugin on a device, emulator or simulator.
// scripts/app/verify-folder-access-*.sh build it as the app (with
// --dart-define=NEUPOD_FOLDER_IT=true) and launch it twice, because a folder
// grant has to survive a restart (`flutter test` uninstalls the app after
// each run, which would drop it):
//
//   first launch   Android: calls the real system picker, which the script
//                  drives with adb (Music/neupod_it). iOS: adds a folder through
//                  debugAddRoot (the picker UI cannot be driven from here).
//                  Walks and reads it. Prints NEUPOD_STEP picked.
//   second launch  the folder is still held after the process was killed; it
//                  is walked, read and removed. Prints
//                  NEUPOD_STEP persisted-and-removed.
//
// Expected tree under the folder: a.flac, sub/b.mp3 (and nothing else that is
// not hidden).

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod_storage/neupod_storage.dart';

const folderName = 'neupod_it';

/// The pick test needs the scripts (on Android, something must drive the
/// system picker), so plain `flutter test integration_test` skips it.
const driven = bool.fromEnvironment('NEUPOD_FOLDER_IT');

Directory iosFixture() {
  // Inside the app container; the test writes it, the app code only reads it.
  final dir = Directory('${Directory.systemTemp.path}/$folderName');
  Directory('${dir.path}/sub').createSync(recursive: true);
  File('${dir.path}/a.flac').writeAsBytesSync([0x66, 0x4c, 0x61, 0x43, 0, 0]);
  File('${dir.path}/sub/b.mp3').writeAsBytesSync([0x49, 0x44, 0x33, 4, 0]);
  File('${dir.path}/.hidden').writeAsBytesSync([0]);
  return dir;
}

Future<void> walkAndRead(NeupodStorage storage, StorageRoot root) async {
  final pages = await storage.walk(root.id, batchSize: 1).toList();
  expect(pages.last.done, isTrue);
  expect(pages.last.complete, isTrue, reason: 'every folder was listed');
  final entries = [for (final p in pages) ...p.entries];
  final byPath = {for (final e in entries) e.relPath: e};
  expect(byPath.keys.toSet(), {'a.flac', 'sub/b.mp3'});
  final flac = byPath['a.flac']!;
  expect(flac.state, EntryState.local);
  expect(flac.size, greaterThan(3));
  expect(flac.modifiedMs, greaterThan(0));
  final reader = await storage.openReader(flac);
  try {
    expect(reader.length, flac.size);
    expect(String.fromCharCodes(reader.readAt(0, 4)), 'fLaC');
  } finally {
    reader.close();
  }
  final mp3 = await storage.openReader(byPath['sub/b.mp3']!);
  try {
    final head = mp3.readAt(0, 3);
    // An ID3v2 tag, or straight into an MPEG frame (sync byte 0xFF).
    expect(String.fromCharCodes(head) == 'ID3' || head[0] == 0xff, isTrue);
  } finally {
    mp3.close();
  }
}

StorageRoot? findTestRoot(List<StorageRoot> roots) {
  for (final r in roots) {
    if (r.kind == RootKind.pickedFolder && r.displayName == folderName) {
      return r;
    }
  }
  return null;
}

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  final storage = NeupodStorage();

  // Under `flutter run` the platform turns semantics on shortly after start;
  // let that happen before the first test, whose end-of-test check would
  // otherwise count the new SemanticsHandle as leaked.
  setUpAll(() => Future<void>.delayed(const Duration(seconds: 2)));

  testWidgets(
    'iOS: Documents is always a root and is walkable',
    (_) async {
      final roots = await storage.roots();
      expect(roots.first.kind, RootKind.appDocuments);
      expect(roots.first.id, 'documents');
      await expectLater(
        storage.removeRoot('documents'),
        throwsA(isA<StorageException>()),
      );
      // A file in Documents (as if copied in through Files or Finder) is
      // listed. The test writes it; the plugin only reads.
      final docs = Directory('${Directory.systemTemp.parent.path}/Documents');
      final copied = Directory('${docs.path}/$folderName')
        ..createSync(recursive: true);
      File('${copied.path}/c.flac').writeAsBytesSync([0x66, 0x4c, 0x61, 0x43]);
      try {
        final paths = await storage
            .walk('documents')
            .expand((p) => p.entries)
            .map((e) => e.relPath)
            .toList();
        expect(paths, contains('$folderName/c.flac'));
      } finally {
        copied.deleteSync(recursive: true);
      }
    },
    skip: !Platform.isIOS,
    semanticsEnabled: false,
  );

  testWidgets(
    'pick, or after a relaunch: still held, then removed',
    (_) async {
      final held = findTestRoot(await storage.roots());
      if (held == null) {
        // First launch: pick.
        final StorageRoot root;
        if (Platform.isIOS) {
          root = await storage.debugAddRoot(iosFixture().path);
        } else {
          final picked = await storage.pickFolder();
          expect(picked, isNotNull, reason: 'the picker was cancelled');
          root = picked!;
        }
        expect(root.displayName, folderName);
        expect(root.available, isTrue);
        expect(findTestRoot(await storage.roots())?.id, root.id);
        await walkAndRead(storage, root);
        // ignore: avoid_print
        print('NEUPOD_STEP picked');
      } else {
        // A later launch: the grant or bookmark was kept across the restart.
        expect(held.available, isTrue);
        await walkAndRead(storage, held);
        await storage.removeRoot(held.id);
        expect(findTestRoot(await storage.roots()), isNull);
        await expectLater(
          storage.walk(held.id).toList(),
          throwsA(isA<StorageException>()),
        );
        // ignore: avoid_print
        print('NEUPOD_STEP persisted-and-removed');
      }
    },
    skip: !driven,
    semanticsEnabled: false,
  );
}
