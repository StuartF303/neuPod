// SPDX-License-Identifier: MPL-2.0

// The Download action for iCloud files that are not on the phone (owner
// decision 7): the request goes to iOS, the row says `Downloading`, the
// state is polled from metadata, and an arrived file is picked up by a scan.

import 'package:flutter/services.dart' show PlatformException;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/app/icloud_download_providers.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod_storage/neupod_storage.dart';
// ignore: implementation_imports
import 'package:neupod_storage/src/messages.g.dart';

import '../fakes/fake_library_scanner.dart';

/// The platform side: records requests and answers states from [states].
class FakeDownloadApi extends NeupodStorageApi {
  final requested = <String>[];
  final states = <String, DownloadState>{};
  final asked = <List<String>>[];
  PlatformException? refuse;
  PlatformException? failStates;

  @override
  Future<void> requestDownload(String uri) async {
    if (refuse != null) throw refuse!;
    requested.add(uri);
  }

  @override
  Future<List<DownloadState>> downloadStates(List<String> uris) async {
    asked.add(uris);
    if (failStates != null) throw failStates!;
    return [for (final u in uris) states[u] ?? DownloadState.notDownloaded];
  }
}

void main() {
  test('each state maps to what the controller does', () {
    expect(downloadStep(DownloadState.notDownloaded), DownloadStep.wait);
    expect(downloadStep(DownloadState.downloading), DownloadStep.wait);
    expect(downloadStep(DownloadState.downloaded), DownloadStep.arrived);
    expect(downloadStep(DownloadState.failed), DownloadStep.drop);
    expect(downloadStep(DownloadState.missing), DownloadStep.drop);
    // Every state has a step (a new one must be mapped here too).
    expect(DownloadState.values.map(downloadStep), hasLength(5));
  });

  group('ICloudDownloads', () {
    late FakeDownloadApi api;
    late FakeLibraryScanner scanner;
    late ProviderContainer container;
    late DateTime clock;

    setUp(() {
      api = FakeDownloadApi();
      scanner = FakeLibraryScanner();
      clock = DateTime(2026, 11, 25, 12);
      container = ProviderContainer.test(
        overrides: [
          neupodStorageProvider.overrideWithValue(NeupodStorage(api: api)),
          libraryScannerProvider.overrideWithValue(scanner),
        ],
      );
      container.read(icloudDownloadsProvider.notifier).now = () => clock;
    });

    ICloudDownloads downloads() =>
        container.read(icloudDownloadsProvider.notifier);
    Set<String> showing() => container.read(icloudDownloadsProvider);

    const a = 'file:///m/a.flac';
    const b = 'file:///m/b.flac';

    test('a request asks iOS once and shows the row as Downloading', () async {
      await downloads().request(a);
      await downloads().request(a);
      expect(api.requested, [a]);
      expect(showing(), {a});
    });

    test('a refused request throws and shows nothing', () async {
      api.refuse = PlatformException(code: 'download-failed');
      await expectLater(
        downloads().request(a),
        throwsA(isA<StorageException>()),
      );
      expect(showing(), isEmpty);
    });

    test('still on its way: keeps waiting, no scan', () async {
      await downloads().request(a);
      api.states[a] = DownloadState.downloading;
      await downloads().poll();
      expect(api.asked, [
        [a],
      ]);
      expect(showing(), {a});
      expect(scanner.started, 0);
    });

    test('arrived: a scan starts, and the row stays Downloading until it '
        'ends', () async {
      await downloads().request(a);
      await downloads().request(b);
      api.states[a] = DownloadState.downloaded;
      await downloads().poll();
      expect(scanner.started, 1);
      expect(showing(), {a, b});

      await scanner.finish(found: 2);
      await pumpEventQueue();
      expect(showing(), {b}, reason: 'the scan has listed a; b still comes');

      // Later polls ask only for b.
      await downloads().poll();
      expect(api.asked.last, [b]);
    });

    test('arrived while a scan runs: waits for it, then scans again', () async {
      await container.read(libraryScanProvider.notifier).start();
      expect(scanner.started, 1);
      await downloads().request(a);
      api.states[a] = DownloadState.downloaded;
      await downloads().poll();
      expect(scanner.started, 1, reason: 'the running scan may have passed a');

      await scanner.finish();
      await pumpEventQueue();
      expect(showing(), {a});
      await downloads().poll();
      expect(scanner.started, 2);
      await scanner.finish();
      await pumpEventQueue();
      expect(showing(), isEmpty);
    });

    test('failed or gone: back to Not downloaded, no scan', () async {
      await downloads().request(a);
      await downloads().request(b);
      api.states[a] = DownloadState.failed;
      api.states[b] = DownloadState.missing;
      await downloads().poll();
      expect(showing(), isEmpty);
      expect(scanner.started, 0);
    });

    test('states that cannot be read count as failed', () async {
      await downloads().request(a);
      api.failStates = PlatformException(code: 'unsupported');
      await downloads().poll();
      expect(showing(), isEmpty);
    });

    test('gives up after giveUpAfter without arriving', () async {
      await downloads().request(a);
      clock = clock.add(
        ICloudDownloads.giveUpAfter - const Duration(seconds: 1),
      );
      await downloads().poll();
      expect(showing(), {a});
      clock = clock.add(const Duration(seconds: 1));
      await downloads().poll();
      expect(showing(), isEmpty);
    });
  });
}
