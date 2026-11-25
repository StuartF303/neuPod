// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/device/device_finish.dart';

/// The device view's switchable choices. Several are the design gate's
/// candidates, switchable at run time so the owner can compare them on real
/// screens; the defaults are what the design drew or recommended. None of
/// the design's Open questions is settled here.
@immutable
class DeviceViewOptions {
  /// The defaults.
  const DeviceViewOptions({
    this.showStatusBar = true,
    this.showWordmark = true,
    this.leftEdgeSwipeIsMenu = true,
    this.haptics = true,
    this.clickSound = false,
  });

  /// The phone's status bar above the device (design section 10): shown, as
  /// recommended, or hidden.
  final bool showStatusBar;

  /// The `neuPod` wordmark on the body (Open question 8). Default on, as
  /// drawn.
  final bool showWordmark;

  /// iOS: a swipe from the left edge is Menu (Open question 9). Default on,
  /// as specified.
  final bool leftEdgeSwipeIsMenu;

  /// The in-app Haptics setting (On by default).
  final bool haptics;

  /// The Click sound setting (Off by default). A seam only in R1's phase 2.
  final bool clickSound;

  /// The body finish follows the mode: silver in Light, black in Dark
  /// (owner decision 13).
  DeviceFinish finishFor(Brightness brightness) =>
      DeviceFinish.forBrightness(brightness);

  /// A copy with other values.
  DeviceViewOptions copyWith({
    bool? showStatusBar,
    bool? showWordmark,
    bool? leftEdgeSwipeIsMenu,
    bool? haptics,
    bool? clickSound,
  }) => DeviceViewOptions(
    showStatusBar: showStatusBar ?? this.showStatusBar,
    showWordmark: showWordmark ?? this.showWordmark,
    leftEdgeSwipeIsMenu: leftEdgeSwipeIsMenu ?? this.leftEdgeSwipeIsMenu,
    haptics: haptics ?? this.haptics,
    clickSound: clickSound ?? this.clickSound,
  );

  @override
  bool operator ==(Object other) =>
      other is DeviceViewOptions &&
      other.showStatusBar == showStatusBar &&
      other.showWordmark == showWordmark &&
      other.leftEdgeSwipeIsMenu == leftEdgeSwipeIsMenu &&
      other.haptics == haptics &&
      other.clickSound == clickSound;

  @override
  int get hashCode => Object.hash(
    showStatusBar,
    showWordmark,
    leftEdgeSwipeIsMenu,
    haptics,
    clickSound,
  );
}

/// Holds the device view's options. In memory for the design gate; Settings
/// persists them later.
class DeviceViewOptionsNotifier extends Notifier<DeviceViewOptions> {
  @override
  DeviceViewOptions build() => const DeviceViewOptions();

  /// Replaces the options.
  void set(DeviceViewOptions value) => state = value;

  /// Changes some options.
  void update(DeviceViewOptions Function(DeviceViewOptions) change) =>
      state = change(state);
}

/// The device view's options.
final deviceViewOptionsProvider =
    NotifierProvider<DeviceViewOptionsNotifier, DeviceViewOptions>(
      DeviceViewOptionsNotifier.new,
    );
