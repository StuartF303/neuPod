// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/library/library_ports.dart';

/// What keeps the library scan going when the app is closed, and shows it
/// outside the app (decision 0009). On Android it is a foreground service
/// with an ongoing notification ("Building library", "N found · N read",
/// Cancel); elsewhere it does nothing ([NoScanKeepAlive]): iOS resumes the
/// scan at the next launch instead.
///
/// The scan controller calls [started] when a scan starts, [progress] as it
/// goes and [stopped] once it has stopped writing. Calls are fire-and-forget
/// and keep their order; a failure never disturbs the scan.
abstract interface class ScanKeepAlive {
  /// A scan started. [askNotifications]: the user started it (Update
  /// Library, a folder added), so it is a fair moment to ask for permission
  /// to show its notification.
  void started(ScanProgress progress, {required bool askNotifications});

  /// The running scan's latest progress.
  void progress(ScanProgress progress);

  /// The scan stopped (done, failed or cancelled) and wrote its last row.
  void stopped();

  /// The user asked outside the app to cancel the scan (the notification's
  /// Cancel).
  Stream<void> get cancelRequests;
}

/// A [ScanKeepAlive] that does nothing: iOS, tests, and any platform without
/// a background service.
final class NoScanKeepAlive implements ScanKeepAlive {
  /// Does nothing.
  const NoScanKeepAlive();

  @override
  void started(ScanProgress progress, {required bool askNotifications}) {}

  @override
  void progress(ScanProgress progress) {}

  @override
  void stopped() {}

  @override
  Stream<void> get cancelRequests => const Stream.empty();
}
