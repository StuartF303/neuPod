// SPDX-License-Identifier: MPL-2.0

/// The app's one library scan: any screen starts, watches or cancels it here,
/// and the library view rebuilds when it finishes (`libraryViewProvider`).
library;

import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/scan_keep_alive.dart';

/// The [ScanProgress.error] of a scan the user cancelled.
const scanCancelled = 'cancelled';

/// What keeps the scan going when the app is closed (decision 0009). Silent
/// by default (iOS, tests); `main.dart` installs the Android scan service
/// (`scanServiceOverrides`).
final scanKeepAliveProvider = Provider<ScanKeepAlive>(
  (ref) => const NoScanKeepAlive(),
);

/// The latest progress of the library scan, or null before the first one.
/// Forwards to [LibraryScanner] (decision 0006); the scanner reads only new
/// or changed files, so starting one is cheap when nothing has changed.
final libraryScanProvider =
    NotifierProvider<LibraryScanController, ScanProgress?>(
      LibraryScanController.new,
    );

/// Owns the scan's subscription: at most one scan runs at a time.
///
/// Tells [scanKeepAliveProvider] when a scan starts, how it goes, and when it
/// has stopped writing, and cancels when it is asked to from outside the app
/// (the Android notification's Cancel).
class LibraryScanController extends Notifier<ScanProgress?> {
  // Cancelled by _stop (restart, cancel, and on dispose).
  // ignore: cancel_subscriptions
  StreamSubscription<ScanProgress>? _sub;

  /// Bumped by every start and cancel; a callback of an older scan is
  /// ignored.
  int _generation = 0;

  /// The cancel of the scan stopped last, until it has stopped: the next
  /// scan waits for it so that two never write at once.
  Future<void>? _stopping;

  bool _byUser = false;

  /// Whether the scan running, or the last one, was started by the user's
  /// Update Library (the firmware's Update Library screen, which reports
  /// `Library updated` when it ends), not by the app itself.
  bool get startedByUser => _byUser;

  /// Whether the user cancelled the last scan.
  bool get cancelledByUser => state?.error == scanCancelled;

  ScanKeepAlive get _keepAlive => ref.read(scanKeepAliveProvider);

  @override
  ScanProgress? build() {
    final keepAlive = _keepAlive;
    final cancels = keepAlive.cancelRequests.listen((_) => unawaited(cancel()));
    ref.onDispose(() {
      unawaited(cancels.cancel());
      _generation++;
      final sub = _sub;
      _sub = null;
      // The database may close under the scan as the app goes: what the
      // stopped scan throws then is of no use to anyone.
      if (sub == null) {
        keepAlive.stopped();
      } else {
        unawaited(_stop(sub).whenComplete(keepAlive.stopped));
      }
    });
    return null;
  }

  /// Whether a scan is running.
  bool get running {
    final s = state;
    return s != null && !s.done;
  }

  /// Starts a scan. When one is running, does nothing, unless [restart]:
  /// then the running one is stopped and a new one started (the folders
  /// changed, so the running walk would miss or keep the wrong ones).
  /// [byUser] marks a scan the user asked for with Update Library.
  Future<void> start({bool restart = false, bool byUser = false}) async {
    if (running && !restart) return;
    final generation = ++_generation;
    _byUser = byUser;
    final old = _sub;
    _sub = null;
    state = const ScanProgress();
    // Now, while the app is in front: Android starts a foreground service
    // only for a visible app.
    _keepAlive.started(state!, askNotifications: byUser || restart);
    if (old != null) await _stop(old);
    final stopping = _stopping;
    if (stopping != null) await stopping;
    if (generation != _generation || !ref.mounted) return;
    _sub = ref
        .read(libraryScannerProvider)
        .scan()
        .listen(
          (p) {
            if (generation != _generation) return;
            state = p;
            if (!p.done) _keepAlive.progress(p);
          },
          onError: (Object e) {
            if (generation != _generation) return;
            _sub = null;
            state = ScanProgress(
              filesFound: state?.filesFound ?? 0,
              filesRead: state?.filesRead ?? 0,
              done: true,
              error: '$e',
            );
            _keepAlive.stopped();
          },
          onDone: () {
            if (generation != _generation) return;
            _sub = null;
            final s = state ?? const ScanProgress();
            if (!s.done) {
              state = ScanProgress(
                filesFound: s.filesFound,
                filesRead: s.filesRead,
                done: true,
                error: s.error,
              );
            }
            _keepAlive.stopped();
          },
          cancelOnError: true,
        );
  }

  /// Starts a scan when there is something to scan: any folder at all (on
  /// iOS the Documents folder is always one, so files copied in with Finder
  /// or the Files app appear without asking). Failing to list the folders
  /// starts nothing. On [resume] (the app back in front), a scan the user
  /// cancelled is not started again: the next launch, Update Library or a
  /// change of folders starts one.
  Future<void> startIfSources({bool resume = false}) async {
    if (resume && cancelledByUser) return;
    try {
      final folders = await ref.read(folderAccessProvider).folders();
      if (!ref.mounted || folders.isEmpty) return;
    } on Object {
      return;
    }
    await start();
  }

  /// Stops the running scan (the user's Select). What it wrote stays; the
  /// state is done with [scanCancelled] as its error.
  Future<void> cancel() async {
    if (!running) {
      // Nothing runs here; make sure nothing is shown as running elsewhere.
      _keepAlive.stopped();
      return;
    }
    final generation = ++_generation;
    final old = _sub;
    _sub = null;
    final s = state!;
    state = ScanProgress(
      filesFound: s.filesFound,
      filesRead: s.filesRead,
      done: true,
      error: scanCancelled,
    );
    if (old != null) await _stop(old);
    // Unless a new scan started meanwhile: it keeps the service.
    if (generation == _generation && ref.mounted) _keepAlive.stopped();
  }

  /// Cancels [sub] and waits for its scan to stop. An error the scan
  /// throws while it stops (it was cancelled; nobody listens) is dropped.
  Future<void> _stop(StreamSubscription<ScanProgress> sub) async {
    final stopping = sub.cancel().catchError((Object _) {});
    _stopping = stopping;
    try {
      await stopping;
    } finally {
      if (identical(_stopping, stopping)) _stopping = null;
    }
  }
}

/// The firmware's `libsvc_status_t` values a failed scan on the phone can be.
enum ScanFailure {
  /// `LIBSVC_FULL`: "Card is full", here "Storage is full" (lead-approved).
  full,

  /// `LIBSVC_DAMAGED`: "Library needs rebuilding".
  damaged,

  /// `LIBSVC_ERROR`: "Card read error", here "Storage read error"
  /// (lead-approved).
  error,
}

/// Which failure [error] (an exception's text) is. A SQLite result code is
/// read as `code & 0xff`, so extended codes count: SQLITE_CORRUPT (11) and
/// SQLITE_NOTADB (26) are a damaged library, SQLITE_FULL (13) a full disk.
ScanFailure scanFailure(String error) {
  final match = RegExp(r'SqliteException\((\d+)').firstMatch(error);
  final code = match == null ? null : int.parse(match.group(1)!) & 0xff;
  if (code == 11 || code == 26) return ScanFailure.damaged;
  final e = error.toLowerCase();
  final full =
      code == 13 ||
      e.contains('sqlite_full') ||
      e.contains('no space left') ||
      e.contains('errno = 28') ||
      e.contains('database or disk is full');
  return full ? ScanFailure.full : ScanFailure.error;
}

/// What a failed scan says: the firmware's `library_status_text()`
/// (`app/runtime/library_service.c` 111-125), with its "Card" made "Storage"
/// where the phone has no card.
String scanFailureText(String error) => switch (scanFailure(error)) {
  ScanFailure.full => 'Storage is full',
  ScanFailure.damaged => 'Library needs rebuilding',
  ScanFailure.error => 'Storage read error',
};

/// The second line under it: the firmware's `hint_for()`
/// (`app/ui/ui_library.c` 504-511). It has a hint only for
/// LIBSVC_NEEDS_WRITABLE, LIBSVC_NO_CARD and LIBSVC_PLAYING (a write-protected
/// card, no card, a player the scan does not wait for); for the three a phone
/// scan can end in it returns NULL, so there is none (an empty string).
String scanFailureHint(String error) => switch (scanFailure(error)) {
  ScanFailure.full || ScanFailure.damaged || ScanFailure.error => '',
};
