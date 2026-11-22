// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/track.dart';

/// Every song, sorted as the firmware sorts (by the repository).
final songsProvider = StreamProvider<List<Track>>(
  (ref) => ref.watch(libraryRepositoryProvider).watchSongs(),
);

/// The latest progress of the scan started from the Songs screen, or null
/// before the first one. Forwards to [LibraryScanner] (decision 0006).
final scanProvider = NotifierProvider<ScanNotifier, ScanProgress?>(
  ScanNotifier.new,
);

class ScanNotifier extends Notifier<ScanProgress?> {
  StreamSubscription<ScanProgress>? _sub;

  @override
  ScanProgress? build() {
    ref.onDispose(() => _sub?.cancel());
    return null;
  }

  /// Starts a scan unless one is running.
  void start() {
    if (_sub != null) return;
    state = const ScanProgress();
    _sub = ref
        .read(libraryScannerProvider)
        .scan()
        .listen(
          (p) => state = p,
          onError: (Object e) {
            state = ScanProgress(
              filesFound: state?.filesFound ?? 0,
              filesRead: state?.filesRead ?? 0,
              done: true,
              error: '$e',
            );
            _sub = null;
          },
          onDone: () => _sub = null,
        );
  }
}
