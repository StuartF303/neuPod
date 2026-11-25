// SPDX-License-Identifier: MPL-2.0

/// Ratings, play counts and "last played": the firmware's rules, ported
/// exactly and checked against the compiled C with vectors
/// (`scripts/app/plays_vectors.c`, `test/domain/library/`).
///
/// - When a track start counts as a play: `app/runtime/play_tracker.c`
///   (`ptrk_update`), fed by `count_plays()` in `app/runtime/player.c`.
/// - What a rating or a play does to a track's numbers: `ratings_set()` in
///   `app/settings/ratings_store.c` and `apply()` in
///   `app/settings/plays_store.c`.
/// - How the wheel sets the stars: `p2_scroll()` in `app/ui/ui_nowplaying.c`.
///
/// What a track's numbers are keyed by, the carry-over on a move, and where
/// they are kept: decision note 0011 and owner decision 20.
library;

/// Heard time that counts a track whose length is not known
/// (`PTRK_UNKNOWN_LEN_MS`).
const int unknownLengthPlayMs = 30000;

/// A track that ended by itself counts after this much heard
/// (`PTRK_MIN_END_MS`).
const int minEndedPlayMs = 3000;

/// The most one pass adds (`PTRK_MAX_STEP_MS`): a pass held up for longer
/// adds only this.
const int maxPassStepMs = 1000;

/// How often the firmware's monitor task runs `count_plays()` (the
/// `os_delay_until_ms(&last, 100)` loop in `app/runtime/runtime.c`).
const Duration playCountPass = Duration(milliseconds: 100);

/// The most stars (`ratings_set` ignores more).
const int maxStars = 5;

/// A rating of this many stars or more notes the track in the plays store
/// (`p2_scroll`: "a well rated track is noted ... so Top rated can open it").
const int noteTrackFromStars = 4;

/// Counted plays stop here (the firmware's `uint16_t plays` saturates).
const int maxPlays = 0xFFFF;

const int _u32 = 0xFFFFFFFF;

/// When does a track count as played: a port of `ptrk_t` and
/// `ptrk_update()` (`app/include/play_tracker.h`,
/// `app/runtime/play_tracker.c`).
///
/// Fed the player's state at every pass, it adds up the time the track was
/// actually playing (a seek or a scan does not move it forward) and says
/// "count it" once per track start, when at least half of the track's length
/// has been heard, or 30 s when the length is not known, or when the track
/// ended by itself after at least 3 s. A new [update] `trackId` starts a new
/// count; pausing stops the clock; a gap between two passes longer than a
/// second adds only a second. Times are milliseconds on a monotonic clock,
/// taken modulo 2^32 as the firmware's `uint32_t` tick is.
final class PlayTracker {
  bool _have = false;
  int _trackId = 0;
  int _playedMs = 0;
  int _lastMs = 0;
  bool _counted = false;

  /// Forgets the track being followed (`ptrk_init`).
  void reset() {
    _have = false;
    _trackId = 0;
    _playedMs = 0;
    _lastMs = 0;
    _counted = false;
  }

  /// One pass at [nowMs]. [playing]: sound is coming out of this track (not
  /// paused, not held back); [ended]: the track ran to its end; [trackId]
  /// identifies the track start; [durationMs] its length, 0 when unknown.
  /// True exactly once per track start, at the pass where it becomes a
  /// counted play.
  bool update({
    required int nowMs,
    required bool playing,
    required bool ended,
    required int trackId,
    required int durationMs,
  }) {
    final now = nowMs & _u32;
    if (!_have || trackId != _trackId) {
      _have = true;
      _trackId = trackId;
      _playedMs = 0;
      _counted = false;
      _lastMs = now;
    }
    final dt = (now - _lastMs) & _u32; // wrap-safe, as uint32_t
    _lastMs = now;
    if (playing) {
      _playedMs =
          (_playedMs + (dt > maxPassStepMs ? maxPassStepMs : dt)) & _u32;
    }
    if (_counted) return false;
    final duration = durationMs & _u32;
    final half = duration != 0 && _playedMs * 2 >= duration;
    final longEnough = duration == 0 && _playedMs >= unknownLengthPlayMs;
    final end = ended && _playedMs >= minEndedPlayMs;
    if (half || longEnough || end) {
      _counted = true;
      return true;
    }
    return false;
  }
}

/// One track's numbers, as the firmware keeps them in its two stores:
/// the rating (`ratings_store`) and the plays entry (`plays_entry_t`).
final class PlayStats {
  /// The numbers.
  const PlayStats({
    this.stars = 0,
    this.plays = 0,
    this.lastPlayed = 0,
    this.stamp = 0,
  });

  /// No rating, never played or noted.
  static const none = PlayStats();

  /// 0 to [maxStars]; 0 is not rated.
  final int stars;

  /// Counted plays, at most [maxPlays].
  final int plays;

  /// The serial of the last counted play; 0 = never played. A bigger number
  /// is more recent: the firmware has no real clock on every board, so "last
  /// played" is a serial that counts up with every play or note
  /// (`plays_store.h`).
  final int lastPlayed;

  /// The serial of the last touch (a play, a rating of [noteTrackFromStars]
  /// or more, an add to On-The-Go); 0 = the plays store has no entry.
  final int stamp;

  /// Nothing worth keeping: not rated and not in the plays store.
  bool get isEmpty => stars == 0 && stamp == 0;

  /// The stars after a rating of [to] (`ratings_set`): 1 to 5 sets it, 0
  /// clears it, anything else changes nothing.
  PlayStats rated(int to) => to < 0 || to > maxStars
      ? this
      : PlayStats(
          stars: to,
          plays: plays,
          lastPlayed: lastPlayed,
          stamp: stamp,
        );

  /// The numbers after the track is noted (`apply` in `plays_store.c`) with
  /// [serial], the store's serial after it was counted up for this touch:
  /// the entry is touched, and with [played] a play is counted (saturating)
  /// and stamped as the latest.
  PlayStats touched(int serial, {required bool played}) => PlayStats(
    stars: stars,
    plays: played && plays < maxPlays ? plays + 1 : plays,
    lastPlayed: played ? serial : lastPlayed,
    stamp: serial,
  );

  @override
  bool operator ==(Object other) =>
      other is PlayStats &&
      other.stars == stars &&
      other.plays == plays &&
      other.lastPlayed == lastPlayed &&
      other.stamp == stamp;

  @override
  int get hashCode => Object.hash(stars, plays, lastPlayed, stamp);

  @override
  String toString() =>
      'PlayStats($stars stars, $plays plays, last $lastPlayed, '
      'stamp $stamp)';
}

/// The stars after the wheel turns [steps] while the rating has the wheel
/// (`p2_scroll`): one star per turn event whatever its size, clockwise more,
/// within 0 to [maxStars].
int starsAfterTurn(int stars, int steps) {
  if (steps == 0) return stars;
  final v = stars + (steps > 0 ? 1 : -1);
  return v < 0 ? 0 : (v > maxStars ? maxStars : v);
}

/// Port: the track statistics as the app keeps them, in its own database,
/// never on a music folder or a card. Tracks are named by their library id
/// (`Song.id`, `QueueItem.id`).
abstract interface class TrackStatsStore {
  /// The track's numbers now, then on every change; [PlayStats.none] for a
  /// track with none, or one the library does not have.
  Stream<PlayStats> watch(String trackId);

  /// Rates the track (0 clears; more than [maxStars] is ignored), and with
  /// [noteTrackFromStars] or more also notes it, as Now Playing does.
  Future<void> rate(String trackId, int stars);

  /// Counts a play of the track (`plays_record(..., played: true)`).
  Future<void> countPlay(String trackId);

  /// Carries numbers from old keys to new ones: files a rescan saw moved or
  /// retagged (owner decision 20). Numbers already at a new key are kept.
  Future<void> carry(List<TrackMove> moves);
}

/// A file a rescan saw at a new key: its path (the device's form, `/` and the
/// path relative to the library folder) or its size changed, and its audio
/// did not.
final class TrackMove {
  /// A move from [fromPath] of [fromSize] bytes to [toPath] of [toSize].
  const TrackMove(this.fromPath, this.fromSize, this.toPath, this.toSize);

  final String fromPath;
  final int fromSize;
  final String toPath;
  final int toSize;

  @override
  bool operator ==(Object other) =>
      other is TrackMove &&
      other.fromPath == fromPath &&
      other.fromSize == fromSize &&
      other.toPath == toPath &&
      other.toSize == toSize;

  @override
  int get hashCode => Object.hash(fromPath, fromSize, toPath, toSize);

  @override
  String toString() => 'TrackMove($fromPath $fromSize -> $toPath $toSize)';
}
