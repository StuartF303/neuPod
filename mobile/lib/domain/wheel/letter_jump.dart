// SPDX-License-Identifier: MPL-2.0

import 'package:neupod/domain/wheel/firmware_constants.dart';

/// When the fast-scroll letter tile and index rail show: an exact port of
/// `wi_letterjump_t`, `wi_letterjump_init()`, `wi_letterjump_scroll()`,
/// `wi_letterjump_tick()` and `wi_letter_first_index()` from
/// `app/ui/input/wheel_input.c`, as used by `app/ui/ui_library.c`,
/// `app/ui/ui_files.c` and `app/ui/ui_rail.c`.
///
/// What the firmware does (this, not the multiplier, is the trigger):
///
/// - The list feeds every move it makes, in **accelerated list steps** (what
///   `ui_accel_feed()` returned), with the initial of the row now selected.
/// - A gap of more than [wiLjWindowMs] (400 ms) since the previous move
///   restarts the count; otherwise the count adds the steps' size.
/// - The overlay becomes [active] once the count reaches [wiLjFastSteps] (6
///   steps), and stays active while moves keep coming.
/// - [tick] (every UI pass) hides it once more than [wiLjHideMs] (250 ms)
///   have passed since the last move, and restarts the count.
/// - `ui_rail.c` then fades the tile and rail out over [uiRailFadeMs]
///   (150 ms, ease-in); a move during the fade shows them at full strength at
///   once.
///
/// Times are unsigned 32-bit milliseconds, as in C.
class LetterJump {
  /// `wi_letterjump_init()`.
  LetterJump();

  /// The tile and rail are showing.
  bool active = false;

  /// `'A'`..`'Z'`, or `'#'` for anything else.
  String letter = '#';

  /// The time of the last move (unsigned 32-bit).
  int lastMs = 0;

  /// Steps counted in the current burst (unsigned 32-bit).
  int energy = 0;

  /// `wi_letterjump_init()`.
  void reset() {
    active = false;
    letter = '#';
    lastMs = 0;
    energy = 0;
  }

  /// `wi_letterjump_scroll()`: the list moved [steps] at [nowMs] and the row
  /// now selected starts with [initial]. Returns [active].
  bool scroll(int steps, int nowMs, String initial) {
    steps = i32(steps);
    nowMs = u32(nowMs);
    if (u32(nowMs - lastMs) > wiLjWindowMs) {
      energy = 0;
    }
    lastMs = nowMs;
    energy = u32(energy + (steps < 0 ? -steps : steps));
    letter = normaliseInitial(initial);
    if (energy >= wiLjFastSteps) {
      active = true;
    }
    return active;
  }

  /// `wi_letterjump_tick()`: call from the UI's tick; hides the overlay after
  /// [wiLjHideMs] without a move.
  void tick(int nowMs) {
    if (active && u32(u32(nowMs) - lastMs) > wiLjHideMs) {
      active = false;
      energy = 0;
    }
  }

  /// The firmware's `norm()`: the first character of [initial], upper-cased
  /// when it is an ASCII letter, else `'#'`. Callers pass the first character
  /// of the row's sort key (the firmware's `letter_of()` uses
  /// `lib_collate_key`, so articles and accents do not count).
  static String normaliseInitial(String initial) {
    if (initial.isEmpty) return '#';
    final c = initial.codeUnitAt(0);
    if (c >= 0x61 && c <= 0x7A) return String.fromCharCode(c - 0x20);
    if (c >= 0x41 && c <= 0x5A) return String.fromCharCode(c);
    return '#';
  }

  /// `wi_letter_first_index()`: the index of the first of [count] items whose
  /// initial is at or after [letter], in a list sorted by normalised initial
  /// (`'#'` first, then A to Z), or the last item when there is none; 0 for an
  /// empty list. [initialOf] gives an item's initial.
  static int firstIndex(
    int count,
    String Function(int index) initialOf,
    String letter,
  ) {
    final want = normaliseInitial(letter);
    var lo = 0;
    var hi = count;
    while (lo < hi) {
      final mid = lo + (hi - lo) ~/ 2;
      final c = normaliseInitial(initialOf(mid));
      final less = c == '#'
          ? want != '#'
          : (want != '#' && c.compareTo(want) < 0);
      if (less) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    // Past the end (no initial at or after [letter]): the last item.
    return lo < count ? lo : (count > 0 ? count - 1 : 0);
  }
}
