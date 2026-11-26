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

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/data/db/drift_library_repository.dart';
import 'package:neupod/data/db/open_library_database.dart';
import 'package:neupod/data/platform/storage_folder_access.dart';
import 'package:neupod/audio/neupod_player.dart';
import 'package:neupod/data/scan/local_library_scanner.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/playback/queue_item.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod_audio/neupod_audio.dart';
import 'package:neupod_storage/neupod_storage.dart';

const folderName = 'neupod_it';

/// The pick test needs the scripts (on Android, something must drive the
/// system picker), so plain `flutter test integration_test` skips it.
const driven = bool.fromEnvironment('NEUPOD_FOLDER_IT');

Future<Directory> iosFixture() async {
  // Inside the app container; the test writes it, the app code only reads it.
  // The bundled test tones (copies of the repo fixtures), so the files play.
  Future<List<int>> asset(String name) async {
    final data = await rootBundle.load(
      'integration_test/assets/test_media/$name',
    );
    return data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes);
  }

  final dir = Directory('${Directory.systemTemp.path}/$folderName');
  Directory('${dir.path}/sub').createSync(recursive: true);
  File('${dir.path}/a.flac')
      .writeAsBytesSync(await asset('flac16_44k_st.flac'));
  File('${dir.path}/sub/b.mp3').writeAsBytesSync(await asset('mp3_44k_st.mp3'));
  File('${dir.path}/.hidden').writeAsBytesSync([0]);
  return dir;
}

/// Spike item 5 of decision 0004: the audio engine plays a file of a picked
/// folder after the app was restarted (Android: the persisted `content://`
/// tree URI; iOS: a `file://` URL under the bookmarked root, whose access
/// only neupod_storage holds).
Future<void> playAfterRestart(StorageEntry flac) async {
  final player = NeupodPlayer(NeupodAudio());
  try {
    await player.setQueue([QueueItem(id: 'it:a', uri: flac.uri, title: 'a')]);
    await player.play();
    final ended = await player.states
        .firstWhere((s) => s.status == PlaybackStatus.ended || s.error != null)
        .timeout(const Duration(seconds: 30));
    expect(ended.error, isNull, reason: 'played ${flac.uri}');
    expect(ended.format?.codec, contains('flac'));
    // ignore: avoid_print
    print('NEUPOD_PLAYED ${flac.uri}');
  } finally {
    await player.dispose();
  }
}

Future<Map<String, StorageEntry>> walkAndRead(
  NeupodStorage storage,
  StorageRoot root,
) async {
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
  return byPath;
}

/// The minimal scan on the device: the real plugin, tags read in a
/// background isolate through the platform's byte source, the database file
/// in the app's private directory, and the Songs order.
Future<void> scanIntoDatabase(NeupodStorage storage) async {
  final db = openLibraryDatabase(storage.appSupportPath);
  try {
    final progress = await LocalLibraryScanner(
      folders: StorageFolderAccess(storage),
      storage: storage,
      db: db,
    ).scan().last;
    expect(progress.done, isTrue);
    final songs = await DriftLibraryRepository(db).watchSongs().first;
    final mine = songs.where((t) => t.source.id != 'documents').toList();
    expect(mine.map((t) => t.displayTitle), [
      'a.flac',
      'b.mp3',
    ]); // untagged: the file name (owner decision 15)
    // Real fixtures, read through a detached fd (Android) or the file URL
    // under the bookmark (iOS).
    expect(mine.map((t) => t.unreadableReason), [null, null]);
    expect(mine.map((t) => t.codec), ['flac', 'mp3']);
    expect(mine.first.durationMs, 300);
    final dir = await storage.appSupportPath();
    expect(File('$dir/library.sqlite').existsSync(), isTrue);
    // ignore: avoid_print
    print('NEUPOD_SCAN ${mine.length} songs in $dir');
  } finally {
    await db.close();
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
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  final storage = NeupodStorage();

  // As in app_launch_test.dart: under `xcodebuild test` iOS turns
  // accessibility on just after launch, and the SemanticsHandle the binding
  // then opens must not land inside a test ("A SemanticsHandle was active at
  // the end of the test"). Wait until it is on (bounded: a device without
  // VoiceOver never turns it on), not a fixed time, which a slow runner
  // outlasts.
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

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

  // Owner decision 7 on the simulator. A real iCloud file that is not
  // downloaded cannot be made here (no iCloud account; the dataless flag is
  // set only by the system), but the placeholder layout can: a hidden
  // `.Name.ext.icloud` file. The walk lists it under its real name with its
  // real URL, the scan lists it greyed without opening it, its state reads
  // not downloaded from metadata, and asking iOS to download a file that is
  // not in iCloud is refused or ignored without touching it. The owner's
  // iPhone check covers a real evicted file (docs/app/PROGRESS.md, phase 3
  // device check).
  testWidgets(
    'iOS: an iCloud placeholder is listed not downloaded and never read',
    (_) async {
      final dir = Directory('${Directory.systemTemp.path}/neupod_icloud_it')
        ..createSync(recursive: true);
      final here = File('${dir.path}/here.flac')
        ..writeAsBytesSync(
          (await rootBundle.load(
            'integration_test/assets/test_media/flac16_44k_st.flac',
          )).buffer.asUint8List(),
        );
      // What iCloud leaves in place of an evicted file: a small plist. Its
      // bytes are not audio, so a read of them would show as damaged.
      final placeholder = File('${dir.path}/.away.flac.icloud')
        ..writeAsBytesSync([0x62, 0x70, 0x6c, 0x69, 0x73, 0x74]);
      final before = placeholder.statSync();
      final root = await storage.debugAddRoot(dir.path);
      final db = openLibraryDatabase(storage.appSupportPath);
      try {
        final entries = await storage
            .walk(root.id)
            .expand((p) => p.entries)
            .toList();
        final byPath = {for (final e in entries) e.relPath: e};
        expect(byPath.keys.toSet(), {'here.flac', 'away.flac'});
        final away = byPath['away.flac']!;
        expect(away.state, EntryState.notDownloaded);
        expect(away.uri, endsWith('/away.flac'));
        expect(away.size, 0);
        await expectLater(
          storage.openHandle(away),
          throwsA(isA<StorageException>()),
        );

        expect(await storage.downloadStates([here.uri.toString(), away.uri]), [
          DownloadState.downloaded,
          DownloadState.notDownloaded,
        ]);

        await LocalLibraryScanner(
          folders: StorageFolderAccess(storage),
          storage: storage,
          db: db,
        ).scan().last.timeout(const Duration(seconds: 60));
        final songs = await DriftLibraryRepository(db).watchSongs().first;
        final reasons = {
          for (final t in songs)
            if (t.source.id == root.id) t.displayTitle: t.unreadableReason,
        };
        expect(reasons, {
          'here.flac': null,
          'away.flac': UnreadableReason.notDownloaded,
        });

        // Queued before it was evicted: the engine checks before it opens a
        // file and skips one that is not on the phone, as it skips an
        // unplayable one (lead call under decisions 7 and 19). The next
        // song plays; the placeholder is never read.
        final player = NeupodPlayer(NeupodAudio());
        try {
          await player.setQueue([
            QueueItem(id: 'it:away', uri: away.uri, title: 'away'),
            QueueItem(id: 'it:here', uri: here.uri.toString(), title: 'here'),
          ]);
          await player.play();
          final ended = await player.states
              .firstWhere(
                (s) => s.status == PlaybackStatus.ended && s.index == 1,
              )
              .timeout(const Duration(seconds: 30));
          expect(ended.queue[ended.index!].id, 'it:here');
          expect(ended.error, isNull);
        } finally {
          await player.dispose();
        }

        // Not an iCloud file: iOS refuses (or ignores) the request. Either
        // way nothing was written.
        try {
          await storage.requestDownload(away.uri);
        } on StorageException catch (e) {
          expect(e.code, 'download-failed');
        }
        await expectLater(
          storage.requestDownload('file:///private/etc/hosts'),
          throwsA(
            isA<StorageException>().having(
              (e) => e.code,
              'code',
              'not-in-root',
            ),
          ),
        );
        final after = placeholder.statSync();
        expect(placeholder.readAsBytesSync(), [
          0x62,
          0x70,
          0x6c,
          0x69,
          0x73,
          0x74,
        ]);
        expect(after.modified, before.modified);
        expect(File('${dir.path}/away.flac').existsSync(), isFalse);
      } finally {
        await db.close();
        await storage.removeRoot(root.id);
        dir.deleteSync(recursive: true);
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
          root = await storage.debugAddRoot((await iosFixture()).path);
        } else {
          final picked = await storage.pickFolder();
          expect(picked, isNotNull, reason: 'the picker was cancelled');
          root = picked!;
        }
        expect(root.displayName, folderName);
        expect(root.available, isTrue);
        expect(findTestRoot(await storage.roots())?.id, root.id);
        await walkAndRead(storage, root);
        await scanIntoDatabase(storage);
        // ignore: avoid_print
        print('NEUPOD_STEP picked');
      } else {
        // A later launch: the grant or bookmark was kept across the restart.
        expect(held.available, isTrue);
        final files = await walkAndRead(storage, held);
        await playAfterRestart(files['a.flac']!);
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
