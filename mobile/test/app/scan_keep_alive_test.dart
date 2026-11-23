// SPDX-License-Identifier: MPL-2.0

// The scan controller drives the scan's keep-alive (decision 0009: the
// Android foreground service): started when a scan starts, told its
// progress, stopped once the scan has stopped writing, and the
// notification's Cancel cancels the scan.

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/app/scan_service_platform.dart';
import 'package:neupod/data/platform/android_scan_service.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/scan_keep_alive.dart';

import '../fakes/fake_library_scanner.dart';
import '../fakes/fake_scan_keep_alive.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late FakeLibraryScanner scanner;
  late FakeScanKeepAlive keepAlive;
  late ProviderContainer container;

  setUp(() {
    scanner = FakeLibraryScanner();
    keepAlive = FakeScanKeepAlive();
    container = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(scanner),
        scanKeepAliveProvider.overrideWithValue(keepAlive),
      ],
    );
  });

  LibraryScanController controller() =>
      container.read(libraryScanProvider.notifier);
  ScanProgress? state() => container.read(libraryScanProvider);

  test(
    'a scan starts the service, reports progress, stops it when done',
    () async {
      await controller().start();
      scanner.say(const ScanProgress(filesFound: 4, filesRead: 1));
      await pumpEventQueue();
      await scanner.finish(found: 4);
      await pumpEventQueue();
      expect(keepAlive.log, ['start 0/0', 'progress 4/1', 'stop']);
    },
  );

  test('only a scan the user started asks for notifications', () async {
    await controller().start(byUser: true);
    await controller().cancel();
    await controller().start(restart: true); // a folder was added
    await controller().cancel();
    await controller().start(); // launch or back in front
    expect(keepAlive.log.where((l) => l.startsWith('start')), [
      'start 0/0 ask',
      'start 0/0 ask',
      'start 0/0',
    ]);
  });

  test('a failed scan stops the service', () async {
    await controller().start();
    scanner.current.addError(StateError('disk gone'));
    await pumpEventQueue();
    expect(keepAlive.log, ['start 0/0', 'stop']);
    expect(state()!.error, contains('disk gone'));
  });

  test(
    'the notification Cancel cancels the scan, then stops the service',
    () async {
      await controller().start();
      scanner.say(const ScanProgress(filesFound: 12, filesRead: 3));
      await pumpEventQueue();
      keepAlive.requestCancel();
      await pumpEventQueue();
      expect(scanner.cancelled, 1);
      expect(state()!.error, scanCancelled);
      expect(controller().cancelledByUser, isTrue);
      expect(keepAlive.log, ['start 0/0', 'progress 12/3', 'stop']);
    },
  );

  test(
    'the service stops only after the cancelled scan stopped writing',
    () async {
      final blocking = _BlockingScanner();
      final log = FakeScanKeepAlive();
      final c = ProviderContainer.test(
        overrides: [
          libraryScannerProvider.overrideWithValue(blocking),
          scanKeepAliveProvider.overrideWithValue(log),
        ],
      );
      final scan = c.read(libraryScanProvider.notifier);
      await scan.start();
      await pumpEventQueue();
      final cancelled = scan.cancel();
      await pumpEventQueue();
      expect(log.log, ['start 0/0', 'progress 1/0'], reason: 'still writing');
      blocking.write.complete();
      await cancelled;
      expect(log.log, ['start 0/0', 'progress 1/0', 'stop']);
    },
  );

  test('a second Cancel waits for the first cancel\'s write too', () async {
    final blocking = _BlockingScanner();
    final log = FakeScanKeepAlive();
    final c = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(blocking),
        scanKeepAliveProvider.overrideWithValue(log),
      ],
    );
    final scan = c.read(libraryScanProvider.notifier);
    await scan.start();
    await pumpEventQueue();
    final first = scan.cancel(); // in the app
    log.requestCancel(); // then the notification's Cancel
    await pumpEventQueue();
    expect(log.log, ['start 0/0', 'progress 1/0'], reason: 'still writing');
    blocking.write.complete();
    await first;
    await pumpEventQueue();
    expect(log.log.last, 'stop');
  });

  test('a cancel while a restart waits for the old write waits too', () async {
    final blocking = _BlockingScanner();
    final log = FakeScanKeepAlive();
    final c = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(blocking),
        scanKeepAliveProvider.overrideWithValue(log),
      ],
    );
    final scan = c.read(libraryScanProvider.notifier);
    await scan.start();
    await pumpEventQueue();
    final restarted = scan.start(restart: true); // waits for the write
    final cancelled = scan.cancel();
    await pumpEventQueue();
    expect(log.log.contains('stop'), isFalse, reason: 'still writing');
    blocking.write.complete();
    await restarted;
    await cancelled;
    expect(log.log.last, 'stop');
  });

  test('a scanner that cannot start stops the service', () async {
    final c = ProviderContainer.test(
      overrides: [
        libraryScannerProvider.overrideWithValue(_ThrowingScanner()),
        scanKeepAliveProvider.overrideWithValue(keepAlive),
      ],
    );
    await c.read(libraryScanProvider.notifier).start();
    expect(keepAlive.log, ['start 0/0', 'stop']);
    expect(c.read(libraryScanProvider)!.error, contains('no scanner'));
  });

  test('a restart keeps the service: no stop between the two scans', () async {
    await controller().start();
    await controller().start(restart: true);
    await scanner.finish();
    await pumpEventQueue();
    expect(keepAlive.log, ['start 0/0', 'start 0/0 ask', 'stop']);
  });

  test('cancel then start: the new scan keeps the service', () async {
    await controller().start();
    final cancelled = controller().cancel();
    final started = controller().start();
    await cancelled;
    await started;
    expect(keepAlive.log, ['start 0/0', 'start 0/0']);
    expect(controller().running, isTrue);
  });

  test('a Cancel with no scan running stops a stray service', () async {
    controller(); // built at launch, as the app does
    keepAlive.requestCancel();
    await pumpEventQueue();
    expect(keepAlive.log, ['stop']);
    expect(scanner.started, 0);
  });

  test('closing the app stops the service', () async {
    await controller().start();
    container.dispose();
    await pumpEventQueue();
    expect(keepAlive.log.last, 'stop');
  });

  group('the Android service', () {
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

    test('speaks the plugin channel in the firmware words', () async {
      final calls = <MethodCall>[];
      messenger.setMockMethodCallHandler(scanServiceChannel, (call) async {
        calls.add(call);
        return null;
      });
      addTearDown(
        () => messenger.setMockMethodCallHandler(scanServiceChannel, null),
      );
      final service = AndroidScanService(
        title: scanNotificationTitle,
        label: (p) => '${p.filesFound} found · ${p.filesRead} read',
        cancel: scanNotificationCancel,
      );
      service.started(const ScanProgress(), askNotifications: true);
      service.progress(const ScanProgress(filesFound: 1204, filesRead: 3));
      service.stopped();
      await pumpEventQueue();
      expect(calls.map((c) => c.method), ['start', 'update', 'stop']);
      expect(calls[0].arguments, {
        'title': 'Building library',
        'text': '0 found · 0 read',
        'cancel': 'Cancel',
        'askNotifications': true,
      });
      expect(calls[1].arguments, {
        'title': 'Building library',
        'text': '1204 found · 3 read',
      });
    });

    test('a failing channel never disturbs the scan', () async {
      messenger.setMockMethodCallHandler(scanServiceChannel, (call) async {
        throw PlatformException(code: 'boom');
      });
      addTearDown(
        () => messenger.setMockMethodCallHandler(scanServiceChannel, null),
      );
      final service = AndroidScanService(
        title: 't',
        label: (_) => 'x',
        cancel: 'c',
      );
      service.started(const ScanProgress(), askNotifications: false);
      service.stopped();
      await pumpEventQueue();
    });

    test('the plugin Cancel reaches the scan', () async {
      final service = AndroidScanService(
        title: 't',
        label: (_) => 'x',
        cancel: 'c',
      );
      final heard = service.cancelRequests.first;
      await messenger.handlePlatformMessage(
        scanServiceChannel.name,
        scanServiceChannel.codec.encodeMethodCall(const MethodCall('cancel')),
        (_) {},
      );
      await heard.timeout(const Duration(seconds: 1));
    });

    test('is installed on Android only', () {
      expect(
        scanServiceOverrides(platform: TargetPlatform.android),
        hasLength(1),
      );
      expect(scanServiceOverrides(platform: TargetPlatform.iOS), isEmpty);
      expect(const NoScanKeepAlive().cancelRequests, emitsDone);
    });
  });
}

/// A scanner that throws before its scan begins.
class _ThrowingScanner implements LibraryScanner {
  @override
  Stream<ScanProgress> scan() => throw StateError('no scanner');
}

/// A scanner whose scan stops inside a write until [write] completes.
class _BlockingScanner implements LibraryScanner {
  final write = Completer<void>();

  @override
  Stream<ScanProgress> scan() async* {
    yield const ScanProgress(filesFound: 1);
    await write.future;
    yield const ScanProgress(filesFound: 1, filesRead: 1, done: true);
  }
}
