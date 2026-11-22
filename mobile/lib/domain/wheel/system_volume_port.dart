// SPDX-License-Identifier: MPL-2.0

/// Whether a volume change worked. Owner decision 10: if the platform's way
/// of setting the volume stops working, the fallback is the owner's to
/// choose, so the app must be able to tell.
enum SystemVolumeStatus {
  /// As asked, or already at the limit (read back from the system).
  ok,

  /// iOS: the hidden volume view had no slider.
  noSlider,

  /// The system's volume did not move.
  noEffect,

  /// The device's volume is fixed (some Android devices).
  fixed,
}

/// The phone's media volume as Now Playing shows it.
class SystemVolume {
  /// A volume of [step] out of [steps].
  const SystemVolume({
    required this.step,
    required this.steps,
    this.fixed = false,
    this.status = SystemVolumeStatus.ok,
  });

  /// Whether the last change worked.
  final SystemVolumeStatus status;

  /// The change did not happen.
  bool get failed => status != SystemVolumeStatus.ok;

  /// The current platform step, 0 to [steps].
  final int step;

  /// How many steps the platform has.
  final int steps;

  /// The volume cannot be changed (some Android devices).
  final bool fixed;

  /// 0 to 1, for the volume bar.
  double get level => steps == 0 ? 0 : step / steps;

  /// Rounded percent, for the screen reader (`62 percent`).
  int get percent => (level * 100).round();
}

/// Sets the phone's own system volume (owner decision 10): on Now Playing one
/// wheel step is one platform volume step. Android: AudioManager; iOS: a
/// hidden MPVolumeView.
abstract interface class SystemVolumePort {
  /// The volume now.
  Future<SystemVolume> read();

  /// One step up ([direction] > 0) or down; returns the volume read back
  /// from the system, whose [SystemVolume.status] says whether it moved.
  Future<SystemVolume> step(int direction);
}

/// A volume that stays where it is put; for tests and the default.
class MemorySystemVolume implements SystemVolumePort {
  /// Starts at [step] of [steps].
  MemorySystemVolume({this.current = 8, this.steps = 16});

  /// The current step.
  int current;

  /// The steps.
  final int steps;

  @override
  Future<SystemVolume> read() async =>
      SystemVolume(step: current, steps: steps);

  @override
  Future<SystemVolume> step(int direction) async {
    current = (current + direction.sign).clamp(0, steps);
    return read();
  }
}
