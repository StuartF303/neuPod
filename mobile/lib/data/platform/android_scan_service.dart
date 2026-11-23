// SPDX-License-Identifier: MPL-2.0

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:neupod/domain/library/library_ports.dart';
import 'package:neupod/domain/library/scan_keep_alive.dart';

/// The channel of the app's scan service plugin
/// (`android/app/src/main/kotlin/.../ScanServicePlugin.kt`).
const scanServiceChannel = MethodChannel(
  'net.stuartfraser.neupod/scan_service',
);

/// The Android scan service (decision 0009): a foreground service that keeps
/// the process, and with it this engine and its scan, alive after the app is
/// swiped away, with an ongoing notification.
class AndroidScanService implements ScanKeepAlive {
  /// The service through [channel]; the notification says [title] (the
  /// firmware's "Building library"), [label] of the progress and [cancel] on
  /// its action.
  AndroidScanService({
    required this.title,
    required this.label,
    required this.cancel,
    this.channel = scanServiceChannel,
  }) {
    channel.setMethodCallHandler((call) async {
      if (call.method == 'cancel') _cancels.add(null);
    });
  }

  /// The channel to the plugin.
  final MethodChannel channel;

  /// The notification's title.
  final String title;

  /// The notification's text for a progress.
  final String Function(ScanProgress) label;

  /// The label of the notification's Cancel action.
  final String cancel;

  final _cancels = StreamController<void>.broadcast();

  @override
  Stream<void> get cancelRequests => _cancels.stream;

  @override
  void started(ScanProgress progress, {required bool askNotifications}) =>
      _call('start', {
        'title': title,
        'text': label(progress),
        'cancel': cancel,
        'askNotifications': askNotifications,
      });

  @override
  void progress(ScanProgress progress) =>
      _call('update', {'title': title, 'text': label(progress)});

  @override
  void stopped() => _call('stop');

  void _call(String method, [Object? arguments]) => unawaited(
    channel.invokeMethod<void>(method, arguments).catchError((Object e) {
      // The scan goes on in the app without the service.
      debugPrint('neupod: scan service $method: $e');
    }),
  );
}
