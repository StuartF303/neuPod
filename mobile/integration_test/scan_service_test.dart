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
//      stops.
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

  testWidgets('the scan completes after the app is swiped away', (
    tester,
  ) async {
    final marker = File('${Directory.systemTemp.path}/neupod_scan_it.done');
    if (marker.existsSync()) marker.deleteSync();
    final scanner = SlowScanner(marker);
    final android = Platform.isAndroid;
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

    final done = Completer<ScanProgress>();
    container.listen(libraryScanProvider, (_, now) {
      if (now != null && now.done && !done.isCompleted) done.complete(now);
    });

    if (android) {
      // The engine must outlive the scan here, so that this test can still
      // report; in the app it is destroyed when the scan ends.
      await scanServiceChannel.invokeMethod<void>(
        'debugKeepEngineAfterScan',
        true,
      );
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

      await scanServiceChannel.invokeMethod<void>('debugFinishActivity');
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
    }

    final end = await done.future.timeout(const Duration(seconds: 60));
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
}
