// SPDX-License-Identifier: MPL-2.0

// The firmware's own tests for the Now Playing controls
// (tests/test_ui_logic.c, "Now Playing: wheel mode, scrub, hold"), case for
// case, with the same names.

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/wheel/firmware_constants.dart';
import 'package:neupod/domain/wheel/now_playing_control.dart';

const _wav = NpcTrack(
  seekable: true,
  posFrames: 44100 * 60,
  totalFrames: 44100 * 240,
  rate: 44100,
);
const _mp3 = NpcTrack(
  seekable: false,
  posFrames: 44100 * 60,
  totalFrames: 44100 * 240,
  rate: 44100,
);

const _volume = NowPlayingWheelMode.volume;
const _seek = NowPlayingWheelMode.seek;

/// Enter seek mode the way the screen does: Select down, held for
/// UI_SELHOLD_MS, seen by the next tick. Returns the tick's events.
int _holdSelect(NowPlayingControl c, NpcTrack t, int t0) {
  expect(c.selectDown(t0), NpcEvent.none);
  return c.tick(t, t0 + uiSelHoldMs);
}

NpcTrack _withId(NpcTrack t, int id) => t.copyWith(id: id);

void main() {
  test('a_short_select_press_opens_page_2_and_a_hold_enters_seek_mode', () {
    final c = NowPlayingControl();
    expect(c.scroll(_wav, 1, 1000), isFalse); // volume mode
    expect(c.selectDown(1000), NpcEvent.none);
    expect(c.tick(_wav, 1000 + uiSelHoldMs - 1), NpcEvent.none);
    expect(c.selectUp(), NpcEvent.page2); // released in time: page 2
    expect(c.mode, _volume);
    expect(_holdSelect(c, _wav, 5000), NpcEvent.seekEnter);
    expect(c.mode, _seek);
    expect(c.selectUp(), NpcEvent.none); // the release of the hold
    expect(c.selectUp(), NpcEvent.none); // a release with no press
  });

  test('select_on_a_track_that_cannot_seek_says_so_and_stays_on_volume', () {
    final c = NowPlayingControl();
    expect(_holdSelect(c, _mp3, 1000), NpcEvent.unavailable);
    expect(c.mode, _volume);
    expect(c.selectUp(), NpcEvent.none);
    expect(c.scroll(_mp3, 3, 1100), isFalse);
    final zero = _wav.copyWith(totalFrames: 0);
    expect(_holdSelect(c, zero, 1200), NpcEvent.unavailable);
  });

  test('seek_mode_scrubs_and_waits_for_select', () {
    final c = NowPlayingControl();
    expect(_holdSelect(c, _wav, 1000), NpcEvent.seekEnter);
    c.selectUp();
    expect(NowPlayingControl.seekStepSeconds(240), 4);
    expect(c.scroll(_wav, 3, 3000), isTrue);
    expect(c.pending, isTrue);
    expect(c.target, (60 + 12) * 44100);
    expect(c.position(_wav), c.target);
    expect(c.scroll(_wav, -1, 3200), isTrue); // from the target
    expect(c.target, (60 + 8) * 44100);
    expect(c.tick(_wav, 3200 + uiSeekCommitMs + 5000), NpcEvent.none);
    expect(c.pending, isTrue);
    expect(c.selectDown(9000), NpcEvent.commit); // "Select: go"
    expect(c.mode, _volume);
    expect(c.target, (60 + 8) * 44100);
    expect(c.selectUp(), NpcEvent.none);
    c.committed();
    expect(c.pending, isFalse);
  });

  test('menu_cancels_seek_mode_and_applies_nothing', () {
    final c = NowPlayingControl();
    expect(c.menu(), isFalse);
    _holdSelect(c, _wav, 1000);
    c.selectUp();
    expect(c.scroll(_wav, 2, 3000), isTrue);
    expect(c.pending, isTrue);
    expect(c.menu(), isTrue);
    expect(c.pending, isFalse);
    expect(c.mode, _volume);
    expect(c.position(_wav), _wav.posFrames);
    expect(c.tick(_wav, 20000), NpcEvent.none);
  });

  test('scrub_clamps_to_the_track', () {
    final c = NowPlayingControl();
    _holdSelect(c, _wav, 0);
    expect(c.scroll(_wav, -1000, 10), isTrue);
    expect(c.target, 0);
    expect(c.scroll(_wav, 100000, 20), isTrue);
    expect(c.target, _wav.totalFrames);
    expect(NowPlayingControl.seekStepSeconds(10), 1);
    expect(NowPlayingControl.seekStepSeconds(3600 * 10), 30);
  });

  test('a_forgotten_seek_mode_is_cancelled_after_ten_seconds', () {
    final c = NowPlayingControl();
    expect(uiSeekIdleMs, 10000);
    _holdSelect(c, _wav, 5000);
    c.selectUp();
    const t0 = 5000 + uiSelHoldMs;
    expect(c.scroll(_wav, 1, t0 + 100), isTrue);
    expect(c.tick(_wav, t0 + 100 + uiSeekIdleMs - 1), NpcEvent.none);
    expect(c.mode, _seek);
    expect(c.tick(_wav, t0 + 100 + uiSeekIdleMs), NpcEvent.toVolume);
    expect(c.mode, _volume);
    expect(c.pending, isFalse);
    expect(c.tick(_wav, 60000), NpcEvent.none);
  });

  test('seek_mode_ends_when_the_track_cannot_seek_any_more', () {
    final c = NowPlayingControl();
    _holdSelect(c, _wav, 0);
    c.selectUp();
    expect(c.scroll(_wav, 1, 1000), isTrue);
    expect(c.tick(_mp3, 1200), NpcEvent.toVolume);
    expect(c.pending, isFalse);
    expect(c.mode, _volume);
  });

  test('a_select_release_that_never_comes_does_not_stick', () {
    final c = NowPlayingControl();
    c.selectDown(0);
    c.tick(_mp3, uiHoldMaxMs);
    expect(c.selDown, isFalse);
    expect(c.selectUp(), NpcEvent.none);
  });

  test('short_press_skips_on_release', () {
    final c = NowPlayingControl();
    c.keyDown(1, _wav, 1000);
    expect(c.tick(_wav, 1000 + uiHoldMs - 1), NpcEvent.none);
    expect(c.keyUp(1, 1000 + uiHoldMs - 1), NpcEvent.skip);
    expect(c.keyUp(1, 1900), NpcEvent.none);
    c.keyDown(-1, _wav, 3000);
    expect(c.keyUp(1, 3010), NpcEvent.none); // the other key
    expect(c.keyUp(-1, 3020), NpcEvent.skip);
  });

  test('hold_right_fast_forwards_and_commits_on_release', () {
    final c = NowPlayingControl();
    c.keyDown(1, _wav, 1000);
    expect(c.tick(_wav, 1000 + uiHoldMs), NpcEvent.none);
    expect(c.holding, isTrue);
    expect(c.pending, isFalse);
    expect(c.tick(_wav, 1000 + uiHoldMs + 1000), NpcEvent.none);
    expect(c.pending, isTrue);
    expect(c.target, (60 + uiFfSpeed) * 44100);
    expect(c.tick(_wav, 1000 + uiHoldMs + 5000), NpcEvent.none);
    expect(c.target, (60 + 5 * uiFfSpeed) * 44100);
    expect(c.keyUp(1, 1000 + uiHoldMs + 5000), NpcEvent.commit);
    expect(c.holding, isFalse);
    expect(c.holdDir, 0);
    c.committed();
  });

  test('hold_left_rewinds_and_stops_at_the_start', () {
    final c = NowPlayingControl();
    c.keyDown(-1, _wav, 0);
    c.tick(_wav, uiHoldMs);
    c.tick(_wav, uiHoldMs + 2000);
    expect(c.target, (60 - 2 * uiFfSpeed) * 44100);
    expect(c.tick(_wav, uiHoldMs + 60000), NpcEvent.commit);
    expect(c.target, 0);
    expect(c.keyUp(-1, uiHoldMs + 60001), NpcEvent.none);
  });

  test('hold_on_a_track_that_cannot_seek_says_so_and_does_not_skip', () {
    final c = NowPlayingControl();
    c.keyDown(1, _wav, 0);
    expect(c.tick(_mp3, uiHoldMs), NpcEvent.unavailable);
    expect(c.tick(_mp3, uiHoldMs + 500), NpcEvent.none);
    expect(c.pending, isFalse);
    expect(c.keyUp(1, uiHoldMs + 600), NpcEvent.none);
  });

  test('seek_timers_survive_the_tick_wrap', () {
    final c = NowPlayingControl();
    const t0 = 0xFFFFFF00;
    expect(_holdSelect(c, _wav, t0), NpcEvent.seekEnter);
    c.selectUp();
    expect(c.scroll(_wav, 1, u32(t0 + 600)), isTrue);
    expect(c.tick(_wav, u32(t0 + 600 + uiSeekIdleMs - 1)), NpcEvent.none);
    expect(c.tick(_wav, u32(t0 + 600 + uiSeekIdleMs)), NpcEvent.toVolume);
  });

  test('a_hold_through_a_track_change_does_not_seek_or_skip_the_new_track', () {
    final c = NowPlayingControl();
    final a = _withId(_wav, 1);
    final b = _withId(_wav, 2);
    c.keyDown(1, a, 1000);
    c.tick(a, 1000 + uiHoldMs);
    c.tick(a, 1000 + uiHoldMs + 2000);
    expect(c.pending, isTrue);
    expect(c.tick(b, 1000 + uiHoldMs + 2100), NpcEvent.none);
    expect(c.pending, isFalse);
    expect(c.holding, isFalse);
    expect(c.keyUp(1, 1000 + uiHoldMs + 2200), NpcEvent.none);
    expect(c.commitValid(b), isFalse);
  });

  test('a_scrub_made_on_another_track_is_not_applied', () {
    final c = NowPlayingControl();
    final a = _withId(_wav, 1);
    final b = _withId(_wav, 2);
    _holdSelect(c, a, 0);
    c.selectUp();
    expect(c.scroll(a, 5, 600), isTrue);
    expect(c.commitValid(a), isTrue);
    expect(c.commitValid(b), isFalse);
    expect(c.tick(b, 620), NpcEvent.none);
    expect(c.pending, isFalse);
    expect(c.scroll(b, 1, 640), isTrue);
    expect(c.target, b.posFrames + 4 * 44100);
    expect(c.id, 2);
  });

  test('another_key_or_the_wheel_drops_a_hold_whose_release_may_be_lost', () {
    final c = NowPlayingControl();
    c.keyDown(1, _wav, 0);
    c.tick(_wav, uiHoldMs);
    c.tick(_wav, uiHoldMs + 1000);
    expect(c.pending, isTrue);
    c.cancelHold();
    expect(c.pending, isFalse);
    expect(c.holdDir, 0);
    expect(c.tick(_wav, uiHoldMs + 5000), NpcEvent.none);
    expect(c.keyUp(1, uiHoldMs + 5001), NpcEvent.none);
    c.keyDown(1, _wav, 9000);
    c.keyDown(-1, _wav, 9100);
    expect(c.keyUp(1, 9200), NpcEvent.none);
    expect(c.keyUp(-1, 9250), NpcEvent.skip);
  });

  test('a_hold_whose_release_never_comes_ends_itself', () {
    final c = NowPlayingControl();
    final big = _wav.copyWith(totalFrames: 44100 * 36000);
    c.keyDown(1, big, 0);
    expect(c.tick(big, uiHoldMs), NpcEvent.none);
    expect(c.tick(big, uiHoldMaxMs - 1), NpcEvent.none);
    expect(c.holding, isTrue);
    expect(c.tick(big, uiHoldMaxMs), NpcEvent.commit);
    expect(c.holding, isFalse);
    expect(c.holdDir, 0);
    expect(c.keyUp(1, uiHoldMaxMs + 100), NpcEvent.none);
  });

  test('a_hold_that_reaches_the_end_of_the_track_applies_it_at_once', () {
    final c = NowPlayingControl();
    c.keyDown(1, _wav, 0);
    c.tick(_wav, uiHoldMs);
    expect(c.tick(_wav, uiHoldMs + 17000), NpcEvent.none);
    expect(c.tick(_wav, uiHoldMs + 19000), NpcEvent.commit);
    expect(c.target, _wav.totalFrames);
    expect(c.holding, isFalse);
    expect(c.keyUp(1, uiHoldMs + 19500), NpcEvent.none);
  });
}
