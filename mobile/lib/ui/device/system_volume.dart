// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';

/// The phone's system volume, which the wheel sets on Now Playing (owner
/// decision 10). The composition root overrides it with the platform plugin;
/// by default (tests, goldens) it is an in-memory volume of 8 of 16 steps.
///
/// For Now Playing (UI engineer 1): in volume mode (when
/// `NowPlayingControl.scroll` returns false), move the volume by the
/// accelerated steps as the firmware does (`bt_volume_step(steps)` in
/// `ui_nowplaying.c`): call [SystemVolumePort.step] once per step, in the
/// step's direction. Give a detent haptic per step that
/// changed the volume, show the device's volume bar with the returned level
/// and hide it 1.2 s after the wheel rests; set the wheel's screen-reader
/// label to `Volume` and value to `<percent> percent`. When a step comes
/// back with `failed` (iOS workaround broken, a fixed-volume device), show
/// the level that was read back and no detent; the fallback is the owner's
/// to choose (owner decision 10).
final systemVolumeProvider = Provider<SystemVolumePort>(
  (ref) => MemorySystemVolume(),
);
