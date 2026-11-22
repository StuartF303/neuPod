// SPDX-License-Identifier: MPL-2.0

/// Now Playing's design-gate switches, and the few things it keeps until the
/// phases that own them land.
library;

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/ui/components/status_bar.dart';

/// Where the rating stars go. The mock-ups have none and the specification
/// requires them, so the owner picks at the gate; both are built.
enum RatingPlacement {
  /// Candidate 1: a Rating row on page 2 only (the firmware's NP-3). Page 1
  /// is as drawn in the mock-ups.
  page2,

  /// Candidate 2: the stars also show at rest, under the format line, as a
  /// display. They are still set on page 2: on page 1 the wheel is the
  /// volume.
  belowFormat,
}

/// The rating placement switch.
class RatingPlacementNotifier extends Notifier<RatingPlacement> {
  @override
  RatingPlacement build() => RatingPlacement.page2;

  /// Chooses a placement.
  void set(RatingPlacement value) => state = value;
}

/// The chosen rating placement.
final ratingPlacementProvider =
    NotifierProvider<RatingPlacementNotifier, RatingPlacement>(
      RatingPlacementNotifier.new,
    );

/// Shuffle, repeat and ratings as Now Playing's page 2 sets them.
///
/// Kept in memory here: the queue rules (phase 4) and the library's ratings
/// (phase 3) own the real ones and replace this provider's state.
@immutable
class PlaybackChoices {
  /// The choices.
  const PlaybackChoices({
    this.shuffle = false,
    this.repeat = RepeatState.off,
    this.ratings = const {},
  });

  /// Shuffle is on.
  final bool shuffle;

  /// Repeat off, all or one.
  final RepeatState repeat;

  /// Stars by track id.
  final Map<String, int> ratings;

  /// A copy with other values.
  PlaybackChoices copyWith({
    bool? shuffle,
    RepeatState? repeat,
    Map<String, int>? ratings,
  }) => PlaybackChoices(
    shuffle: shuffle ?? this.shuffle,
    repeat: repeat ?? this.repeat,
    ratings: ratings ?? this.ratings,
  );
}

/// Holds the [PlaybackChoices].
class PlaybackChoicesNotifier extends Notifier<PlaybackChoices> {
  @override
  PlaybackChoices build() => const PlaybackChoices();

  /// Shuffle on or off.
  void toggleShuffle() => state = state.copyWith(shuffle: !state.shuffle);

  /// Repeat off, then all, then one.
  void cycleRepeat() => state = state.copyWith(
    repeat: RepeatState.values[(state.repeat.index + 1) % 3],
  );

  /// Sets the stars (0 to 5) of a track.
  void rate(String trackId, int stars) => state = state.copyWith(
    ratings: {...state.ratings, trackId: stars.clamp(0, 5)},
  );
}

/// Shuffle, repeat and ratings.
final playbackChoicesProvider =
    NotifierProvider<PlaybackChoicesNotifier, PlaybackChoices>(
      PlaybackChoicesNotifier.new,
    );

/// The last time the system volume would not move, for the gate and debug
/// builds (owner decision 10: if the platform's way of setting the volume
/// stops working, the fallback is the owner's to choose, so it must be seen).
@immutable
class VolumeFailure {
  /// A failure of [status], the [count]th since the app started.
  const VolumeFailure(this.status, this.count);

  /// Why it did not move.
  final SystemVolumeStatus status;

  /// How many steps have failed.
  final int count;

  @override
  String toString() => 'Volume step failed: ${status.name} (x$count)';
}

/// Holds the latest [VolumeFailure], null while every step worked.
class VolumeFailureNotifier extends Notifier<VolumeFailure?> {
  @override
  VolumeFailure? build() => null;

  /// Records a failed step.
  void record(SystemVolumeStatus status) {
    state = VolumeFailure(status, (state?.count ?? 0) + 1);
    debugPrint('neupod: $state');
  }

  /// Forgets the failure (a step worked again).
  void clear() => state = null;
}

/// The latest system-volume failure, if any: the gate shows it.
final volumeFailureProvider =
    NotifierProvider<VolumeFailureNotifier, VolumeFailure?>(
      VolumeFailureNotifier.new,
    );
