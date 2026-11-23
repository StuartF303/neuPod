// SPDX-License-Identifier: MPL-2.0

// The library scan survives its activity going away (decision 0009), on the
// Android emulator, under instrumentation (tool/android_integration_tests.sh):
//
//   1. a scan starts through the app's scan controller, with the real scan
//      service (the foreground service and its plugin);
//   2. the test finishes the activity and removes its task, as swiping the
//      app away from Recents does;
//   3. the scan goes on in the kept engine and completes: it writes its
//      completion marker and the controller ends done; then the service
//      stops. A folder pick left open by the swipe-away ends as cancelled.
//   4. (second test) the app is launched again mid-scan: the new activity
//      takes the kept engine and the scan's progress goes on; Add Folder's
//      picker opens again (not refused as busy); the notification's Cancel
//      cancels the scan and the service stops.
//
// The scanner is a stand-in that walks no folder (an emulator under
// instrumentation cannot grant one: the system picker needs driving from
// outside, scripts/app/verify-folder-access-android.sh), so what is proven is
// the service and the engine, not the scanner. On iOS (no service; the scan
// resumes at the next launch) the same scan simply completes in the app.

import 'dart:async';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/app/scan_service_platform.dart';
import 'package:neupod/data/platform/android_scan_service.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/scan_keep_alive.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// A scan that takes a while: [steps] files, one every [step], then a marker
/// file, then done.
class SlowScanner implements LibraryScanner {
  SlowScanner(
    this.marker, {
    this.steps = 40,
    this.step = const Duration(milliseconds: 250),
  });

  final File marker;
  final int steps;
  final Duration step;

  @override
  Stream<ScanProgress> scan() async* {
    for (var i = 1; i <= steps; i++) {
      await Future<void>.delayed(step);
      yield ScanProgress(filesFound: i, filesRead: i);
    }
    marker.writeAsStringSync('scanned $steps');
    yield ScanProgress(filesFound: steps, filesRead: steps, done: true);
  }
}

Future<Map<String, Object?>> debugState() async =>
    (await scanServiceChannel.invokeMapMethod<String, Object?>('debugState'))!;

/// Polls the scan service's state until [test] holds.
Future<Map<String, Object?>> until(
  bool Function(Map<String, Object?> s) test,
  String what,
) async {
  final deadline = DateTime.now().add(const Duration(seconds: 30));
  var s = await debugState();
  while (!test(s)) {
    if (DateTime.now().isAfter(deadline)) {
      throw TimeoutException('$what not reached; last: $s');
    }
    await Future<void>.delayed(const Duration(milliseconds: 100));
    s = await debugState();
  }
  return s;
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // As in app_launch_test.dart: on iOS wait for accessibility before a test.
  setUpAll(() async {
    if (!Platform.isIOS) return;
    final deadline = DateTime.now().add(const Duration(seconds: 10));
    while (!binding.platformDispatcher.semanticsEnabled &&
        DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
  });

  final android = Platform.isAndroid;

  /// A container with [scanner] and, on Android, the real scan service.
  ProviderContainer containerWith(LibraryScanner scanner) {
    final ScanKeepAlive keepAlive = android
        ? AndroidScanService(
            title: scanNotificationTitle,
            label: scanProgressLabel,
            cancel: scanNotificationCancel,
          )
        : const NoScanKeepAlive();
    final container = ProviderContainer(
      overrides: [
        libraryScannerProvider.overrideWithValue(scanner),
        scanKeepAliveProvider.overrideWithValue(keepAlive),
      ],
    );
    addTearDown(container.dispose);
    return container;
  }

  Future<ScanProgress> doneOf(ProviderContainer container) {
    final done = Completer<ScanProgress>();
    container.listen(libraryScanProvider, (_, now) {
      if (now != null && now.done && !done.isCompleted) done.complete(now);
    });
    return done.future;
  }

  Future<void> debug(String method, [Object? arguments]) =>
      scanServiceChannel.invokeMethod<void>(method, arguments);

  testWidgets('the scan completes after the app is swiped away', (
    tester,
  ) async {
    final marker = File('${Directory.systemTemp.path}/neupod_scan_it.done');
    if (marker.existsSync()) marker.deleteSync();
    final scanner = SlowScanner(marker, steps: 60);
    final container = containerWith(scanner);
    final done = doneOf(container);

    if (android) {
      // The engine must outlive the scan here, so that this test can still
      // report; in the app it is destroyed when the scan ends.
      await debug('debugKeepEngineAfterScan', true);
      // With the activity gone no frame comes; the binding's own pumps
      // (after the test body) must not wait for one.
      binding.framePolicy =
          LiveTestWidgetsFlutterBindingFramePolicy.benchmarkLive;
    }

    await container.read(libraryScanProvider.notifier).start(byUser: true);

    if (android) {
      final up = await until(
        (s) => s['serviceRunning'] == true,
        'scan service running',
      );
      expect(up['scanActive'], isTrue);
      expect(up['activityAttached'], isTrue);

      // Add Folder's picker is open when the app is swiped away.
      final pick = NeupodStorage().pickFolder();
      await Future<void>.delayed(const Duration(seconds: 2));

      await debug('debugFinishActivity');
      final gone = await until(
        (s) => s['activityAttached'] == false,
        'activity gone',
      );
      final atDetach = container.read(libraryScanProvider)!;
      // ignore: avoid_print
      print('NEUPOD_SCAN activity gone at ${atDetach.filesFound} found; $gone');
      expect(gone['engineKept'], isTrue, reason: 'the engine was kept');
      expect(gone['serviceRunning'], isTrue);
      expect(atDetach.done, isFalse, reason: 'the scan was still running');
      expect(atDetach.filesFound, lessThan(scanner.steps));
      expect(
        await pick.timeout(const Duration(seconds: 15)),
        isNull,
        reason: 'the pick left open ends as cancelled',
      );
    }

    final end = await done.timeout(const Duration(seconds: 60));
    expect(end.error, isNull);
    expect(end.filesFound, scanner.steps);
    expect(marker.readAsStringSync(), 'scanned ${scanner.steps}');

    if (android) {
      final after = await until(
        (s) => s['serviceRunning'] == false && s['scanActive'] == false,
        'scan service stopped',
      );
      expect(after['activityAttached'], isFalse);
      // ignore: avoid_print
      print('NEUPOD_SCAN completed with no activity; $after');
    }
  });

  testWidgets(
    'launched again mid-scan, the app takes the kept engine; Cancel works',
    (tester) async {
      // The first test left its engine kept, with no activity.
      var s = await debugState();
      expect(s['activityAttached'], isFalse);
      expect(s['engineKept'], isTrue);
      await debug('debugRelaunch');
      s = await until((s) => s['activityAttached'] == true, 'relaunched');
      expect(s['engineKept'], isFalse, reason: 'the activity took it');

      final marker = File('${Directory.systemTemp.path}/neupod_scan_it2.done');
      if (marker.existsSync()) marker.deleteSync();
      final scanner = SlowScanner(marker, steps: 400);
      final container = containerWith(scanner);
      final done = doneOf(container);
      await container.read(libraryScanProvider.notifier).start(byUser: true);
      await until((s) => s['serviceRunning'] == true, 'scan service running');

      await debug('debugFinishActivity');
      s = await until((s) => s['activityAttached'] == false, 'activity gone');
      expect(s['engineKept'], isTrue);
      final beforeRelaunch = container.read(libraryScanProvider)!.filesFound;

      await debug('debugRelaunch');
      s = await until((s) => s['activityAttached'] == true, 'relaunched');
      expect(s['engineKept'], isFalse, reason: 'the activity took it');
      expect(s['serviceRunning'], isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 1500));
      final afterRelaunch = container.read(libraryScanProvider)!;
      // ignore: avoid_print
      print(
        'NEUPOD_SCAN relaunched: $beforeRelaunch found before, '
        '${afterRelaunch.filesFound} after',
      );
      expect(afterRelaunch.done, isFalse);
      expect(afterRelaunch.filesFound, greaterThan(beforeRelaunch));

      // Add Folder works after the relaunch: the picker opens (it was
      // refused as busy when a swipe-away left a pick pending).
      Object? pickError;
      var pickEnded = false;
      final pick = NeupodStorage().pickFolder().then<void>(
        (_) {
          pickEnded = true;
        },
        onError: (Object e) {
          pickError = e;
        },
      );
      await Future<void>.delayed(const Duration(seconds: 2));
      expect(pickError, isNull, reason: 'the picker opened');

      // The notification's Cancel.
      await debug('debugNotificationCancel');
      final end = await done.timeout(const Duration(seconds: 30));
      expect(end.error, scanCancelled);
      expect(end.filesFound, lessThan(scanner.steps));
      expect(marker.existsSync(), isFalse);
      await until(
        (s) => s['serviceRunning'] == false && s['scanActive'] == false,
        'scan service stopped',
      );

      // Closing the app with the picker open ends the pick as cancelled.
      await debug('debugFinishActivity');
      await pick.timeout(const Duration(seconds: 15));
      expect(pickError, isNull);
      expect(pickEnded, isTrue);
    },
    skip: !Platform.isAndroid, // no service on iOS
  );
}
