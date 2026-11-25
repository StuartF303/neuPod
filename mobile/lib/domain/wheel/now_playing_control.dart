// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/wheel/firmware_constants.dart';

/// What the wheel does on Now Playing (`ui_wheel_mode_t`).
enum NowPlayingWheelMode {
  /// The wheel sets the volume (`UI_WHEEL_VOLUME`).
  volume,

  /// The wheel moves the seek target (`UI_WHEEL_SEEK`).
  seek,
}

/// Event flags returned by [NowPlayingControl]'s calls (a bit set), as
/// `UI_NPC_*` in `app/ui/ui_logic.h`.
abstract final class NpcEvent {
  /// Nothing (`UI_NPC_NONE`).
  static const int none = 0;

  /// Apply [NowPlayingControl.target] now (`UI_NPC_COMMIT`).
  static const int commit = 1 << 0;

  /// The wheel went back to volume (`UI_NPC_TO_VOLUME`).
  static const int toVolume = 1 << 1;

  /// A Previous / Next press that was not a hold: skip (`UI_NPC_SKIP`).
  static const int skip = 1 << 2;

  /// A hold on a track that cannot seek: say "Seek not available"
  /// (`UI_NPC_UNAVAILABLE`).
  static const int unavailable = 1 << 3;

  /// A short Select press was released: open page 2 (`UI_NPC_PAGE2`).
  static const int page2 = 1 << 4;

  /// The Select hold entered seek mode: give the hold haptic
  /// (`UI_NPC_SEEK_ENTER`).
  static const int seekEnter = 1 << 5;
}

/// What the playing track offers (`ui_npc_track_t`).
class NpcTrack {
  /// A track description; positions are in frames at [rate] per second.
  const NpcTrack({
    required this.seekable,
    required this.posFrames,
    required this.totalFrames,
    required this.rate,
    this.id = 0,
  });

  /// The player can seek in it.
  final bool seekable;

  /// The playing position, in frames.
  final int posFrames;

  /// The length, in frames (0 when unknown).
  final int totalFrames;

  /// Frames per second.
  final int rate;

  /// Changes when another track plays: a scrub or hold never carries over.
  final int id;

  /// A copy with other values.
  NpcTrack copyWith({
    bool? seekable,
    int? posFrames,
    int? totalFrames,
    int? id,
  }) => NpcTrack(
    seekable: seekable ?? this.seekable,
    posFrames: posFrames ?? this.posFrames,
    totalFrames: totalFrames ?? this.totalFrames,
    rate: rate,
    id: id ?? this.id,
  );
}

/// The Now Playing controls: wheel mode (volume or seek), seek mode,
/// Previous / Next scan while held. An exact port of `ui_npc_t` and the
/// `ui_npc_*` functions of `app/ui/ui_logic.c` (owner decision of
/// 12 November 2026 for the device): Select short press = page 2; Select held
/// [uiSelHoldMs] = seek mode, where the wheel moves the target, Select applies
/// it and Menu drops it; Previous / Next held [uiHoldMs] scan at [uiFfSpeed]
/// times and apply [uiSeekCommitMs] after release.
///
/// Times are unsigned 32-bit milliseconds, so the timers survive the clock
/// wrapping, as in C.
///
/// The app feeds it from the wheel's raw button events: Select down and up
/// from the centre; Previous / Next down and up from the ring. A press the
/// wheel cancels (it became a turn, or the finger slid off the centre) is
/// passed as [cancelHold] for Previous / Next, and as [selectUp] with its
/// result ignored for Select (the port adds no call of its own).
class NowPlayingControl {
  /// `ui_npc_init()`.
  NowPlayingControl();

  /// The wheel's mode.
  NowPlayingWheelMode mode = NowPlayingWheelMode.volume;

  /// The last seek-mode input.
  int modeMs = 0;

  /// The target moved and is not applied yet: show it instead of the playing
  /// position.
  bool pending = false;

  /// The seek target, in frames.
  int target = 0;

  /// When the target last moved.
  int moveMs = 0;

  /// Previous (-1) or Next (+1) is down; 0 for neither.
  int holdDir = 0;

  /// ... and has been for [uiHoldMs] already.
  bool holding = false;

  /// When it went down.
  int holdMs = 0;

  /// The last scan step.
  int stepMs = 0;

  /// The track the pending target belongs to.
  int id = 0;

  /// The track the hold started on.
  int holdId = 0;

  /// Select is down (its press began a possible hold).
  bool selDown = false;

  /// ... and was already used: the release does nothing.
  bool selUsed = false;

  /// When Select went down.
  int selMs = 0;

  static bool _elapsedGe(int nowMs, int sinceMs, int ms) =>
      u32(nowMs - sinceMs) >= ms;

  static bool _can(NpcTrack t) =>
      t.seekable && t.rate != 0 && t.totalFrames != 0;

  /// `ui_seek_step_s()`: track seconds one (accelerated) wheel step moves in
  /// seek mode: about 1/60 of the track, 1 to 30 s.
  static int seekStepSeconds(int totalSeconds) {
    final s = totalSeconds ~/ 60;
    return s < 1 ? 1 : (s > 30 ? 30 : s);
  }

  /// `ui_npc_position()`: the position to draw.
  int position(NpcTrack t) => pending ? target : t.posFrames;

  void _moveTarget(NpcTrack t, int deltaFrames, int nowMs) {
    if (pending && id != t.id) {
      pending = false; // a scrub of the track before
    }
    var to = position(t) + deltaFrames;
    if (to < 0) to = 0;
    if (to > t.totalFrames) to = t.totalFrames; // the end itself
    target = to;
    pending = true;
    id = t.id;
    moveMs = u32(nowMs);
  }

  /// `ui_npc_select_down()`: in seek mode that is "go"; otherwise it starts a
  /// possible hold.
  int selectDown(int nowMs) {
    selDown = true;
    selMs = u32(nowMs);
    if (mode == NowPlayingWheelMode.seek) {
      mode = NowPlayingWheelMode.volume;
      selUsed = true; // its release must not open page 2
      return pending ? NpcEvent.commit : NpcEvent.toVolume;
    }
    selUsed = false;
    return NpcEvent.none;
  }

  /// `ui_npc_select_up()`: [NpcEvent.page2] for a short press not used for
  /// anything else.
  int selectUp() {
    final shortPress = selDown && !selUsed;
    selDown = false;
    selUsed = false;
    return shortPress ? NpcEvent.page2 : NpcEvent.none;
  }

  /// `ui_npc_menu()`: in seek mode, drop the target and leave seek mode
  /// (true: Menu is consumed); otherwise false (Menu goes back as usual).
  bool menu() {
    if (mode != NowPlayingWheelMode.seek) return false;
    mode = NowPlayingWheelMode.volume;
    pending = false; // "Menu: cancel"
    return true;
  }

  /// `ui_npc_scroll()`: [steps] already accelerated. In seek mode they move
  /// the target (true); otherwise the caller changes the volume (false).
  bool scroll(NpcTrack t, int steps, int nowMs) {
    if (mode != NowPlayingWheelMode.seek) return false;
    modeMs = u32(nowMs);
    if (_can(t)) {
      _moveTarget(
        t,
        steps * seekStepSeconds(t.totalFrames ~/ t.rate) * t.rate,
        nowMs,
      );
    }
    return true;
  }

  /// `ui_npc_cancel_hold()`: forget a Previous / Next that is down.
  void cancelHold() {
    if (holding) {
      pending = false; // a scan not finished by a release: nothing applied
    }
    holdDir = 0;
    holding = false;
  }

  /// `ui_npc_commit_valid()`.
  bool commitValid(NpcTrack t) => pending && id == t.id;

  /// `ui_npc_key_down()`: Previous ([dir] < 0) or Next went down.
  void keyDown(int dir, NpcTrack t, int nowMs) {
    cancelHold(); // a second key while one is down
    holdId = t.id;
    holdDir = dir < 0 ? -1 : 1;
    holding = false;
    holdMs = u32(nowMs);
    stepMs = u32(nowMs);
  }

  /// `ui_npc_key_up()`: [NpcEvent.skip] for a short press,
  /// [NpcEvent.commit] when a hold moved the target.
  int keyUp(int dir, int nowMs) {
    final d = dir < 0 ? -1 : 1;
    if (holdDir != d) return NpcEvent.none; // its press was not seen here
    final wasHold = holding;
    holdDir = 0;
    holding = false;
    if (!wasHold) return NpcEvent.skip;
    return pending ? NpcEvent.commit : NpcEvent.none;
  }

  /// `ui_npc_tick()`: the Select hold, the idle cancel of seek mode, the
  /// commit of a scan once it stopped, the scan while held.
  int tick(NpcTrack t, int nowMs) {
    nowMs = u32(nowMs);
    var ev = NpcEvent.none;
    final can = _can(t);
    if (pending && id != t.id) {
      pending = false; // the track changed under a scrub or a hold
    }
    if (holdDir != 0 && holdId != t.id) {
      cancelHold(); // its release does nothing either
    }
    if (holdDir != 0 && !holding && _elapsedGe(nowMs, holdMs, uiHoldMs)) {
      holding = true;
      stepMs = nowMs;
      if (!can) ev |= NpcEvent.unavailable;
    }
    if (holding && can) {
      final dt = u32(nowMs - stepMs);
      if (dt != 0) {
        stepMs = nowMs;
        _moveTarget(t, holdDir * (t.rate * uiFfSpeed * dt ~/ 1000), nowMs);
      }
      // The hold ends itself at the start or the end of the track.
      if (pending && (target == 0 || target >= t.totalFrames)) {
        holdDir = 0;
        holding = false;
        return ev | NpcEvent.commit;
      }
    }
    if (holding && _elapsedGe(nowMs, holdMs, uiHoldMaxMs)) {
      holdDir = 0;
      holding = false;
      return ev | (pending ? NpcEvent.commit : NpcEvent.none);
    }
    if (selDown &&
        !selUsed &&
        mode != NowPlayingWheelMode.seek &&
        _elapsedGe(nowMs, selMs, uiSelHoldMs)) {
      selUsed = true; // the hold fired: the release is not a short press
      if (can) {
        mode = NowPlayingWheelMode.seek;
        modeMs = nowMs;
        ev |= NpcEvent.seekEnter;
      } else {
        ev |= NpcEvent.unavailable;
      }
    }
    if (selDown && _elapsedGe(nowMs, selMs, uiHoldMaxMs)) {
      selDown = false; // a release that never arrives
      selUsed = false;
    }
    if (mode == NowPlayingWheelMode.seek &&
        (!can || (!holding && _elapsedGe(nowMs, modeMs, uiSeekIdleMs)))) {
      mode = NowPlayingWheelMode.volume; // cancelled, nothing applied
      pending = false;
      ev |= NpcEvent.toVolume;
    }
    if (pending &&
        !holding &&
        can &&
        mode != NowPlayingWheelMode.seek &&
        _elapsedGe(nowMs, moveMs, uiSeekCommitMs)) {
      ev |= NpcEvent.commit; // a Previous / Next scan that stopped
    }
    if (pending && !can) {
      pending = false; // the track changed under a scrub: drop it
    }
    return ev;
  }

  /// `ui_npc_committed()`: the target was handed to the player.
  void committed() {
    pending = false;
    modeMs = moveMs; // the idle clock counts from the last input
  }
}
