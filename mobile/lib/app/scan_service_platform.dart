// SPDX-License-Identifier: MPL-2.0

/// The scan's platform service (decision 0009): on Android, the foreground
/// service that keeps the scan going after the app is swiped away, with its
/// notification in the firmware's words. `main.dart` installs it; iOS and
/// tests keep the silent default (iOS resumes the scan at the next launch).
library;

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/misc.dart' show Override;
import 'package:neupod/app/library_scan_providers.dart';
import 'package:neupod/data/platform/android_scan_service.dart';
import 'package:neupod/ui/screens/browse/library_screen.dart';

/// The firmware's heading of a running build (`ui_library.c`, the Update
/// Library pane).
const scanNotificationTitle = 'Building library';

/// The notification's action that stops the scan: the firmware's
/// `Select: cancel`.
const scanNotificationCancel = 'Cancel';

/// The overrides `main.dart` installs: the Android scan service on Android,
/// nothing elsewhere.
List<Override> scanServiceOverrides({TargetPlatform? platform}) {
  if (kIsWeb || (platform ?? defaultTargetPlatform) != TargetPlatform.android) {
    return const [];
  }
  return [
    scanKeepAliveProvider.overrideWithValue(
      AndroidScanService(
        title: scanNotificationTitle,
        label: scanProgressLabel,
        cancel: scanNotificationCancel,
      ),
    ),
  ];
}
