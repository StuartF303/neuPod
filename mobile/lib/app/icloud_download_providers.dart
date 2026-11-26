// SPDX-License-Identifier: MPL-2.0

/// iCloud files that are not on the phone (iOS; owner decision 7): listed
/// greyed `Not downloaded`; Select asks iOS to download one. The app never
/// fetches a file itself and never reads one that is not downloaded.
///
/// Once asked, iOS fetches the file on its own. This controller then asks
/// for the file's download state every [ICloudDownloads.pollEvery] (a
/// metadata read, never the file), shows the row as `Downloading` meanwhile,
/// and when the file is on the phone runs the library scan, which reads only
/// new or changed files, so the song comes back with its tags and art. The
/// scan waits until no file has arrived for [ICloudDownloads.rescanAfter],
/// so songs tapped one after another share one walk. If the scan still sees
/// a file as not downloaded (its metadata lagging the state read), it is
/// kept `Downloading` and scanned once more.
///
/// Why a poll and not `NSMetadataQuery`: a metadata query sees only the
/// app's own iCloud container (it needs the iCloud entitlement and a
/// container, which the app does not have and R1 does not want), not a
/// folder the user picked in iCloud Drive. A file presenter per file would
/// work but is more native state for the same result. Only the files the
/// user asked for are polled, only while the app is in front (iOS suspends
/// timers in the background, and every return to the front scans anyway),
/// and for at most [ICloudDownloads.giveUpAfter].
library;

import 'dart:async';

import 'package:flutter/foundation.dart' show visibleForTesting;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/data/db/library_database.dart';
import 'package:neupod_storage/neupod_storage.dart';

/// The URIs of songs the user has asked iOS to download whose new state the
/// library does not show yet: their rows say `Downloading`.
final icloudDownloadsProvider = NotifierProvider<ICloudDownloads, Set<String>>(
  ICloudDownloads.new,
);

/// Which of the given URIs the library still lists as not downloaded (read
/// after a scan; tests stand in).
final stillNotDownloadedProvider =
    Provider<Future<Set<String>> Function(List<String> uris)>(
      (ref) => ref.watch(libraryDatabaseProvider).urisNotDownloaded,
    );

/// What the controller does with a file in each [DownloadState].
enum DownloadStep {
  /// Still on its way: ask again at the next poll.
  wait,

  /// On the phone: a scan picks it up.
  arrived,

  /// iOS gave up, or the file has gone: stop showing `Downloading`; the
  /// row is `Not downloaded` again (or gone after the next scan).
  drop,
}

/// [DownloadStep] for [state].
DownloadStep downloadStep(DownloadState state) => switch (state) {
  DownloadState.notDownloaded || DownloadState.downloading => DownloadStep.wait,
  DownloadState.downloaded => DownloadStep.arrived,
  DownloadState.failed || DownloadState.missing => DownloadStep.drop,
};

class ICloudDownloads extends Notifier<Set<String>> {
  /// How often the files asked for are checked.
  static const pollEvery = Duration(seconds: 2);

  /// How long a file is shown `Downloading` without arriving.
  static const giveUpAfter = Duration(minutes: 10);

  /// How long after the last arrival the scan starts.
  static const rescanAfter = Duration(milliseconds: 2500);

  final _asked = <String, DateTime>{};

  /// Arrived, waiting for a scan to start.
  final _arrived = <String>{};
  DateTime? _lastArrival;

  /// Scanned once already but still not downloaded in the library.
  final _retried = <String>{};

  /// Arrived and covered by the scan running now: dropped when it ends.
  final _inScan = <String>{};
  Timer? _timer;

  /// For tests: the clock.
  @visibleForTesting
  DateTime Function() now = DateTime.now;

  NeupodStorage get _storage => ref.read(neupodStorageProvider);

  @override
  Set<String> build() {
    ref.listen(libraryScanProvider.select((s) => s?.done ?? false), (was, now) {
      if (was == false && now && _inScan.isNotEmpty) {
        final scanned = _inScan.toList();
        _inScan.clear();
        unawaited(_afterScan(scanned));
      }
    });
    ref.onDispose(() => _timer?.cancel());
    return const {};
  }

  /// Asks iOS to download the song at [uri]. Throws [StorageException] when
  /// iOS refuses; the row then stays `Not downloaded`.
  Future<void> request(String uri) async {
    if (state.contains(uri)) return;
    await _storage.requestDownload(uri);
    if (!ref.mounted) return;
    _asked[uri] = now();
    state = {...state, uri};
    _timer ??= Timer.periodic(pollEvery, (_) => unawaited(poll()));
  }

  bool _polling = false;

  /// One check of the files still on their way; starts a scan for those that
  /// have arrived. Called by the timer.
  @visibleForTesting
  Future<void> poll() async {
    if (_polling) return;
    _polling = true;
    try {
      final waiting = _asked.keys.toList();
      if (waiting.isNotEmpty) {
        List<DownloadState> states;
        try {
          states = await _storage.downloadStates(waiting);
        } on StorageException {
          states = [for (final _ in waiting) DownloadState.failed];
        }
        if (!ref.mounted) return;
        final drop = <String>[];
        final t = now();
        for (final (i, uri) in waiting.indexed) {
          switch (downloadStep(states[i])) {
            case DownloadStep.arrived:
              _asked.remove(uri);
              _arrive(uri);
            case DownloadStep.drop:
              _asked.remove(uri);
              drop.add(uri);
            case DownloadStep.wait:
              if (t.difference(_asked[uri]!) >= giveUpAfter) {
                _asked.remove(uri);
                drop.add(uri);
              }
          }
        }
        _remove(drop);
      }
      final scan = ref.read(libraryScanProvider.notifier);
      final settled =
          _lastArrival != null &&
          now().difference(_lastArrival!) >= rescanAfter;
      if (_arrived.isNotEmpty && settled && !scan.running) {
        _inScan.addAll(_arrived);
        _arrived.clear();
        await scan.start();
      }
      if (_asked.isEmpty && _arrived.isEmpty) {
        _timer?.cancel();
        _timer = null;
      }
    } finally {
      _polling = false;
    }
  }

  void _arrive(String uri) {
    _arrived.add(uri);
    _lastArrival = now();
  }

  /// After the scan that covered [scanned]: the rows it now lists as
  /// playable are done; one still not downloaded is scanned once more, then
  /// left to the library's word.
  Future<void> _afterScan(List<String> scanned) async {
    Set<String> away;
    try {
      away = await ref.read(stillNotDownloadedProvider)(scanned);
    } on Object {
      away = {};
    }
    if (!ref.mounted) return;
    final done = <String>[];
    for (final uri in scanned) {
      if (away.contains(uri) && _retried.add(uri)) {
        _arrive(uri);
      } else {
        _retried.remove(uri);
        done.add(uri);
      }
    }
    _remove(done);
    if (_arrived.isNotEmpty) {
      _timer ??= Timer.periodic(pollEvery, (_) => unawaited(poll()));
    }
  }

  void _remove(List<String> uris) {
    if (uris.isEmpty || !ref.mounted) return;
    state = {...state}..removeAll(uris);
  }
}
