// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/device/device_accessibility.dart';

/// The phone's Reduce Transparency setting (iOS; Android has no such setting
/// and reads false). Flutter exposes no such flag, so the `neupod_device`
/// plugin reads it (`systemReduceTransparencyProvider`, through the
/// `AccessibilityPort` that `main.dart` overrides). Custom mode paints every
/// part solid and loads no board picture while it is true, and the part
/// editor greys the Transparency field. A plain value, so a test can
/// override it.
final reduceTransparencyProvider = Provider<bool>(
  (ref) => ref.watch(systemReduceTransparencyProvider) ?? false,
);

/// Whether the platform has answered yet. Until it has, the device view
/// paints solid and loads no board picture, so Reduce Transparency never
/// costs a picture decode on its first frames.
final reduceTransparencyKnownProvider = Provider<bool>(
  (ref) => ref.watch(systemReduceTransparencyProvider) != null,
);
