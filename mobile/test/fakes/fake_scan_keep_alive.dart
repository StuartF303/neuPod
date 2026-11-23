// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/scan_keep_alive.dart';

/// A [ScanKeepAlive] that records its calls in [log] and cancels on
/// [requestCancel], as the notification's Cancel does.
class FakeScanKeepAlive implements ScanKeepAlive {
  /// `start 0/0 ask`, `progress 4/1`, `stop`, in order.
  final List<String> log = [];

  final _cancels = StreamController<void>.broadcast();

  /// The notification's Cancel.
  void requestCancel() => _cancels.add(null);

  @override
  Stream<void> get cancelRequests => _cancels.stream;

  @override
  void started(ScanProgress progress, {required bool askNotifications}) =>
      log.add(
        'start ${progress.filesFound}/${progress.filesRead}'
        '${askNotifications ? ' ask' : ''}',
      );

  @override
  void progress(ScanProgress progress) =>
      log.add('progress ${progress.filesFound}/${progress.filesRead}');

  @override
  void stopped() => log.add('stop');
}
