// SPDX-License-Identifier: MPL-2.0

// The app's one scan: started once, restarted when the folders change,
// cancelled, started at launch only when there is something to scan, and
// the library view rebuilt when it finishes.

import 'dart:async';

import 'package:drift/native.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod/domain/library/library_folder.dart';
import 'package:neupod/domain/library/library_ports.dart';

import '../fakes/fake_folder_access.dart';
import '../fakes/fake_library_scanner.dart';

const documents = LibraryFolder(
  id: 'documents',
  name: 'neuPod',
  kind: FolderKind.appDocuments,
  available: true,
);

void main() {
  late FakeLibraryScanner scanner;
  late FakeFolderAccess access;
  late ProviderContainer container;

  setUp(() {
    scanner = FakeLibraryScanner();
    access = FakeFolderAccess();
    container = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(scanner),
        folderAccessProvider.overrideWithValue(access),
      ],
    );
  });

  LibraryScanController controller() =>
      container.read(libraryScanProvider.notifier);
  ScanProgress? state() => container.read(libraryScanProvider);

  test('one scan at a time; progress, then done', () async {
    expect(state(), isNull);
    await controller().start();
    await controller().start();
    expect(scanner.started, 1);
    expect(controller().running, isTrue);

    scanner.say(const ScanProgress(filesFound: 4, filesRead: 1));
    await pumpEventQueue();
    expect(state(), const ScanProgress(filesFound: 4, filesRead: 1));

    await scanner.finish(found: 4);
    await pumpEventQueue();
    expect(state()!.done, isTrue);
    expect(controller().running, isFalse);

    await controller().start();
    expect(scanner.started, 2, reason: 'a finished scan can run again');
  });

  test('restart stops the running scan and starts another', () async {
    await controller().start();
    scanner.say(const ScanProgress(filesFound: 9));
    await pumpEventQueue();
    await controller().start(restart: true);
    expect(scanner.started, 2);
    expect(scanner.cancelled, 1);
    expect(state(), const ScanProgress());

    // The old scan's late words are not heard.
    scanner.scans.first.add(const ScanProgress(filesFound: 99));
    await pumpEventQueue();
    expect(state(), const ScanProgress());
  });

  test('cancel stops the scan and keeps its counts', () async {
    await controller().start();
    scanner.say(const ScanProgress(filesFound: 12, filesRead: 3));
    await pumpEventQueue();
    await controller().cancel();
    expect(scanner.cancelled, 1);
    expect(
      state(),
      const ScanProgress(
        filesFound: 12,
        filesRead: 3,
        done: true,
        error: scanCancelled,
      ),
    );
    expect(controller().running, isFalse);
  });

  test('a failing scan ends done, with its error', () async {
    await controller().start();
    scanner.current.addError(StateError('disk gone'));
    await pumpEventQueue();
    expect(state()!.done, isTrue);
    expect(state()!.error, contains('disk gone'));
  });

  test('startIfSources scans only when there is a folder', () async {
    await controller().startIfSources();
    expect(scanner.started, 0);

    access.held.add(documents);
    await controller().startIfSources();
    expect(scanner.started, 1);
  });

  test('a finished scan rebuilds the library view', () async {
    final db = LibraryDatabase(NativeDatabase.memory());
    addTearDown(db.close);
    final c = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(scanner),
        libraryDatabaseProvider.overrideWithValue(db),
      ],
    );
    c.listen(libraryViewProvider, (_, _) {});
    final before = await c.read(libraryViewProvider.future);

    await c.read(libraryScanProvider.notifier).start();
    scanner.say(const ScanProgress(filesFound: 2));
    await pumpEventQueue();
    expect(await c.read(libraryViewProvider.future), same(before));

    await scanner.finish(found: 2);
    await pumpEventQueue();
    expect(await c.read(libraryViewProvider.future), isNot(same(before)));
  });
  test(
    'back in front, a scan the user cancelled is not started again',
    () async {
      access.held.add(documents);
      await controller().start();
      await controller().cancel();
      expect(controller().cancelledByUser, isTrue);

      await controller().startIfSources(resume: true);
      expect(scanner.started, 1, reason: 'cancelled: resume leaves it');

      // A launch (or Update Library, or a folder change) starts one as usual.
      await controller().startIfSources();
      expect(scanner.started, 2);
      await scanner.finish();
      await pumpEventQueue();

      // A scan that finished is followed by one on resume.
      await controller().startIfSources(resume: true);
      expect(scanner.started, 3);
    },
  );

  test('a restart waits for the running scan to stop writing', () async {
    final blocking = _BlockingScanner();
    final c = ProviderContainer.test(
      overrides: [libraryScannerProvider.overrideWithValue(blocking)],
    );
    final scan = c.read(libraryScanProvider.notifier);
    await scan.start();
    await pumpEventQueue();
    expect(blocking.log, ['start 1', 'write 1'], reason: 'inside a write');

    final restarted = scan.start(restart: true);
    await pumpEventQueue();
    expect(blocking.log, ['start 1', 'write 1'], reason: 'the second waits');
    expect(c.read(libraryScanProvider), const ScanProgress());

    blocking.write.complete(); // the first scan's write ends; it stops
    await restarted;
    await pumpEventQueue();
    expect(blocking.log, ['start 1', 'write 1', 'end 1', 'start 2', 'end 2']);
    expect(blocking.overlapped, isFalse);
    expect(c.read(libraryScanProvider)!.done, isTrue);
  });

  test('startedByUser follows the last start', () async {
    await controller().start(byUser: true);
    expect(controller().startedByUser, isTrue);
    await controller().start(restart: true);
    expect(controller().startedByUser, isFalse);
  });

  test('a failure reads as the firmware status text', () {
    expect(
      scanFailureText('SqliteException(13): database or disk is full'),
      'Storage is full',
    );
    expect(
      scanFailureText('FileSystemException: No space left on device'),
      'Storage is full',
    );
    expect(
      scanFailureText('FolderAccessException(no-walk)'),
      'Storage read error',
    );
  });
  test('a scan that fails while the app goes reports nothing', () async {
    // As on iOS CI: the database closes under a launch scan when the app
    // (the container) goes; the stopped scan's error must not escape.
    final closing = _FailsAfterGate();
    final c = ProviderContainer(
      overrides: [libraryScannerProvider.overrideWithValue(closing)],
    );
    await c.read(libraryScanProvider.notifier).start();
    await pumpEventQueue();
    c.dispose();
    closing.gate.complete();
    await pumpEventQueue();
    expect(closing.threw, isTrue);
  });
}

/// A scanner that throws once [gate] completes, as a scan does when its
/// database is closed under it.
class _FailsAfterGate implements LibraryScanner {
  final gate = Completer<void>();
  bool threw = false;

  @override
  Stream<ScanProgress> scan() async* {
    yield const ScanProgress(filesFound: 1);
    await gate.future;
    threw = true;
    throw StateError('Channel was closed before receiving a response');
  }
}

/// A scanner whose first scan stops inside a write until [write] completes,
/// as the real one does between two yields (an `async*` generator only
/// stops at a yield). [log] records each scan's start, write and end.
class _BlockingScanner implements LibraryScanner {
  final write = Completer<void>();
  final log = <String>[];
  int _started = 0;
  int _active = 0;
  bool overlapped = false;

  @override
  Stream<ScanProgress> scan() async* {
    final n = ++_started;
    log.add('start $n');
    if (++_active > 1) overlapped = true;
    try {
      yield const ScanProgress(filesFound: 1);
      if (n == 1) {
        log.add('write $n');
        await write.future;
      }
      yield const ScanProgress(filesFound: 1, filesRead: 1, done: true);
    } finally {
      _active--;
      log.add('end $n');
    }
  }
}
