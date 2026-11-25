// SPDX-License-Identifier: MPL-2.0

/// Now Playing wired to the player and the wheel.
///
/// The wheel's raw events feed `NowPlayingControl` (the firmware's port: seek
/// mode, scan, the select hold); the rules are not repeated here. In volume
/// mode the wheel sets the phone's system volume through
/// `systemVolumeProvider` (owner decision 10).
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/play_stats_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library/play_stats.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/wheel/now_playing_control.dart';
import 'package:neupod/domain/wheel/system_volume_port.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/domain/wheel/wheel_input.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/device_view.dart' show gateSwitchesEnabled;
import 'package:neupod/ui/device/system_volume.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/cover_art.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/screens/shell/shell_style.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_format.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_options.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_view.dart';
import 'package:neupod/ui/style/backdrop.dart';

/// The volume bar returns to the progress bar this long after the wheel rests
/// (device view design, section 5).
const Duration volumeBarHold = Duration(milliseconds: 1200);

/// How long a status-bar message stays (UI styles design: about two seconds).
const Duration messageHold = Duration(seconds: 2);

/// How often `NowPlayingControl.tick` runs.
const Duration _tickEvery = Duration(milliseconds: 50);

/// The Now Playing screen.
class NowPlayingScreen extends ConsumerStatefulWidget {
  /// Now Playing. Menu on page 1 goes back through the shell;
  /// [onOpenQueue] is the Queue row of page 2 (the queue screen is not built
  /// yet).
  const NowPlayingScreen({this.onOpenQueue, super.key});

  /// The route to the queue.
  final VoidCallback? onOpenQueue;

  @override
  ConsumerState<NowPlayingScreen> createState() => _NowPlayingScreenState();
}

class _NowPlayingScreenState extends ConsumerState<NowPlayingScreen>
    with WidgetsBindingObserver {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  late final int Function() _now = ref.read(wheelClockProvider);
  final NowPlayingControl _npc = NowPlayingControl();
  ScreenHandle? _screen;
  bool _claimed = false;
  StreamSubscription<Duration>? _positions;
  Timer? _ticker;
  Timer? _volumeTimer;
  Timer? _messageTimer;
  Map<WheelButton, int> _savedHolds = const {};
  Map<WheelButton, int> _ownHolds = const {};
  int _describedAt = 0;

  Duration _position = Duration.zero;
  bool _page2 = false;
  bool _swallowPress = false;
  Page2Row _focus = Page2Row.rating;
  bool _ratingEditing = false;
  (String, int)? _pendingStars;
  int _ratingWrites = 0;
  Future<void> _ratingChain = Future.value();
  SystemVolume? _volume;
  bool _volumeShown = false;
  String? _message;
  bool _holdingBefore = false;
  Future<void> _volumeChain = Future.value();

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    // A panel over the screen: a held Select, Previous or Next must not go on
    // to enter seek mode or scan unseen (its release goes to the panel), and
    // when it goes this screen says what the wheel is again.
    ref.listenManual(shellProvider.select((s) => s.panel != null), (was, isUp) {
      if (isUp) {
        _dropHalfDone();
        if (mounted) setState(() {});
      } else {
        _syncHolds();
        _describe(force: true);
      }
    });
    final player = ref.read(playerProvider);
    _position = player.state.position;
    _positions = player.positions().listen((p) {
      if (mounted) setState(() => _position = p);
    });
    ref
        .read(systemVolumeProvider)
        .read()
        .then((v) {
          if (mounted) setState(() => _volume = v);
          _describe();
        })
        .catchError((Object e) {
          if (mounted) _recordThrown('read', e);
        });
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _screen) {
      _screen?.isTop.removeListener(_onTop);
      if (_screen?.wheel == _onWheel) _screen?.wheel = null;
      _screen = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_onTop);
      WidgetsBinding.instance.addPostFrameCallback((_) => _onTop());
    }
  }

  bool get _isTop => _screen?.isTop.value ?? true;

  /// This screen became the one on top, or stopped being: take or give up the
  /// wheel's holds, haptic ownership and screen-reader text.
  void _onTop() {
    if (!mounted) return;
    if (_isTop) {
      _claim();
    } else {
      _release();
    }
  }

  void _claim() {
    if (_claimed) return;
    _claimed = true;
    _savedHolds = Map.of(_bus.holdMs);
    _bus.screenGivesHoldHaptic.addAll(const {
      WheelButton.previous,
      WheelButton.next,
      WheelButton.centre,
    });
    _syncHolds();
    _describe(force: true);
  }

  void _release() {
    _dropHalfDone();
    if (!_claimed) return;
    _claimed = false;
    // Put the holds back only if they are still ours: another screen may
    // have replaced them first.
    if (mapEquals(_bus.holdMs, _ownHolds)) {
      _bus.holdMs
        ..clear()
        ..addAll(_savedHolds);
    }
    _bus.screenGivesHoldHaptic.removeAll(const {
      WheelButton.previous,
      WheelButton.next,
      WheelButton.centre,
    });
  }

  /// A hold or a scan is dropped and seek mode is left with nothing applied.
  void _dropHalfDone() {
    _npc
      ..cancelHold()
      ..selectUp();
    if (_npc.mode == NowPlayingWheelMode.seek) _npc.menu();
    _npc.pending = false;
    _holdingBefore = false;
    _stopTick();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _screen?.isTop.removeListener(_onTop);
    if (_screen?.wheel == _onWheel) _screen?.wheel = null;
    _positions?.cancel();
    _ticker?.cancel();
    _volumeTimer?.cancel();
    _messageTimer?.cancel();
    _release();
    super.dispose();
  }

  // ---- the track ---------------------------------------------------------

  PlayerState get _state => ref.read(playerProvider).state;

  NpcTrack get _track {
    final s = _state;
    final total = s.duration?.inMilliseconds ?? 0;
    final item = s.current;
    return NpcTrack(
      seekable:
          s.status == PlaybackStatus.ready &&
          s.error == null &&
          s.duration != null &&
          total > 0,
      posFrames: _position.inMilliseconds,
      totalFrames: total,
      rate: 1000, // frames are milliseconds
      id: Object.hash(item?.id, s.index),
    );
  }

  Duration get _shownPosition =>
      Duration(milliseconds: _npc.position(_track).clamp(0, 1 << 40));

  // ---- wheel -------------------------------------------------------------

  /// The holds the wheel watches: Previous and Next scan and the centre seeks
  /// on page 1 (the centre drops out in seek mode, where Select is "go").
  void _syncHolds() {
    if (!_claimed) return;
    _bus.holdMs.clear();
    if (!_page2) {
      _bus.holdMs.addAll(WheelInput.nowPlayingHoldMs);
      if (_npc.mode == NowPlayingWheelMode.seek) {
        _bus.holdMs.remove(WheelButton.centre);
      }
    }
    _ownHolds = Map.of(_bus.holdMs);
  }

  // ---- the tick ----------------------------------------------------------

  /// `NowPlayingControl.tick` has something to do: a hold, a scan, seek mode
  /// or a target not yet applied.
  bool get _needsTick =>
      _npc.selDown ||
      _npc.holdDir != 0 ||
      _npc.pending ||
      _npc.mode == NowPlayingWheelMode.seek;

  void _ensureTick() {
    if (_ticker != null || !_needsTick) return;
    _ticker = Timer.periodic(_tickEvery, (_) => _tick());
  }

  void _stopTick() {
    _ticker?.cancel();
    _ticker = null;
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.paused) return;
    // Backgrounded: nothing half-done survives, and every timer stops.
    _dropHalfDone();
    _volumeTimer?.cancel();
    _messageTimer?.cancel();
    if (mounted) {
      setState(() {
        _volumeShown = false;
        _message = null;
      });
      _syncHolds();
      _describe(force: true);
    }
  }

  /// The shell hands the top screen every wheel event; true when used. What
  /// is not taken, the shell does: Menu goes back, Play/Pause drives the
  /// player.
  bool _onWheel(WheelEvent e) {
    if (!_isTop) return false;
    if (_page2) return _onEventPage2(e);
    var used = true;
    final now = _now();
    switch (e) {
      case WheelStep(:final steps):
        _onStep(steps, now);
      case WheelButtonDown(:final button):
        switch (button) {
          case WheelButton.centre:
            _handle(_npc.selectDown(now));
          case WheelButton.previous:
            _npc.keyDown(-1, _track, now);
          case WheelButton.next:
            _npc.keyDown(1, _track, now);
          case _:
            break;
        }
      case WheelButtonUp(:final button):
        switch (button) {
          case WheelButton.centre:
            final r = _npc.selectUp();
            if ((r & NpcEvent.page2) != 0) _openPage2();
          case WheelButton.previous:
            _handle(_npc.keyUp(-1, now), skip: -1);
          case WheelButton.next:
            _handle(_npc.keyUp(1, now), skip: 1);
          case _:
            break;
        }
      case WheelButtonCancelled(:final button):
        switch (button) {
          case WheelButton.centre:
            _npc.selectUp(); // the port adds no call of its own; ignored
          case WheelButton.previous || WheelButton.next:
            _npc.cancelHold();
          case _:
            break;
        }
      case WheelPress(:final button):
        if (_swallowPress && button == WheelButton.centre) {
          _swallowPress = false;
          return true;
        }
        switch (button) {
          case WheelButton.menu:
            if (_npc.menu()) {
              _syncHolds();
              setState(() {});
              _describe();
            } else {
              used = false; // the shell goes back
            }
          case WheelButton.playPause:
            used = false; // the shell plays or pauses
          case _:
            break;
        }
      case WheelHoldStart() || WheelHoldEnd():
        break;
    }
    _refresh();
    return used;
  }

  void _onStep(int steps, int now) {
    final feedback = ref.read(deviceFeedbackProvider);
    if (_npc.scroll(_track, steps, now)) {
      feedback.detent();
      return;
    }
    // Volume mode: one wheel step is one platform volume step.
    final direction = steps.sign;
    for (var i = 0; i < steps.abs(); i++) {
      _volumeChain = _volumeChain.then((_) => _stepVolume(direction));
    }
  }

  Future<void> _stepVolume(int direction) async {
    if (!mounted) return;
    final before = _volume?.step;
    final SystemVolume result;
    try {
      result = await ref.read(systemVolumeProvider).step(direction);
    } on Object catch (e) {
      if (mounted) _recordThrown('step', e);
      return;
    }
    if (!mounted) return;
    if (result.failed) {
      // Owner decision 10: the fallback is the owner's. The bar shows the
      // level read back from the system, there is no detent, and the failure
      // is made visible in debug and gate builds and the gate panel.
      ref.read(volumeFailureProvider.notifier).record(result.status);
      _flashFailure(result.status.name);
    } else {
      if (ref.read(volumeFailureProvider) != null) {
        ref.read(volumeFailureProvider.notifier).clear();
      }
      if (before != result.step) ref.read(deviceFeedbackProvider).detent();
    }
    setState(() {
      _volume = result;
      _volumeShown = true;
    });
    _volumeTimer?.cancel();
    _volumeTimer = Timer(volumeBarHold, () {
      if (mounted) setState(() => _volumeShown = false);
    });
    _describe(force: true);
  }

  /// The port threw: a different thing from a step that came back failed.
  void _recordThrown(String what, Object error) {
    debugPrint('neupod: volume $what threw: $error');
    ref.read(volumeFailureProvider.notifier).record(null, error: error);
    _flashFailure('error');
  }

  /// Debug and gate builds say so in the status bar; a store build, which has
  /// no gate, stays quiet apart from the bar showing the read-back level.
  void _flashFailure(String reason) {
    if (kDebugMode || gateSwitchesEnabled) {
      _say("Can't set volume: $reason");
    }
  }

  void _say(String text) {
    _messageTimer?.cancel();
    setState(() => _message = text);
    _messageTimer = Timer(messageHold, () {
      if (mounted) setState(() => _message = null);
    });
  }

  void _handle(int ev, {int skip = 0}) {
    if ((ev & NpcEvent.skip) != 0) {
      final player = ref.read(playerProvider);
      if (skip < 0) {
        player.skipToPrevious();
      } else {
        player.skipToNext();
      }
    }
    if ((ev & NpcEvent.commit) != 0) _commit();
    if ((ev & NpcEvent.unavailable) != 0) _say('Seek not available');
    if ((ev & NpcEvent.seekEnter) != 0) {
      ref.read(deviceFeedbackProvider).holdTookEffect();
    }
  }

  void _commit() {
    final track = _track;
    if (_npc.commitValid(track)) {
      final to = Duration(milliseconds: _npc.target);
      ref.read(playerProvider).seek(to);
      _position = to;
    }
    _npc.committed();
  }

  void _tick() {
    if (!mounted || _page2) {
      _stopTick();
      return;
    }
    final ev = _npc.tick(_track, _now());
    if (ev != NpcEvent.none) _handle(ev);
    final holding = _npc.holding;
    if (holding && !_holdingBefore && _track.seekable) {
      ref.read(deviceFeedbackProvider).holdTookEffect();
    }
    _holdingBefore = holding;
    if (ev != NpcEvent.none || _npc.pending || holding) _refresh();
    if (!_needsTick) _stopTick();
  }

  void _refresh() {
    if (!mounted) return;
    _syncHolds();
    setState(() {});
    _describe();
    _ensureTick();
  }

  // ---- page 2 ------------------------------------------------------------

  void _openPage2({bool swallowPress = true}) {
    _page2 = true;
    // The Press that follows the centre's release is not a page 2 press.
    _swallowPress = swallowPress;
    _focus = Page2Row.rating;
    _ratingEditing = false;
  }

  bool _onEventPage2(WheelEvent e) {
    var used = true;
    final feedback = ref.read(deviceFeedbackProvider);
    switch (e) {
      case WheelStep(:final steps):
        if (_ratingEditing) {
          // p2_scroll: one star per turn whatever its size, 0 to 5, saved
          // at once; a step only when the stars changed.
          final id = _state.current?.id;
          if (id != null) {
            final now = _rating();
            final to = starsAfterTurn(now, steps);
            if (to != now) {
              _rate(id, to);
              feedback.detent();
            }
          }
        } else {
          final to = (_focus.index + steps).clamp(
            0,
            Page2Row.values.length - 1,
          );
          if (to != _focus.index) {
            _focus = Page2Row.values[to];
            feedback.detent();
          } else {
            feedback.endReached();
          }
        }
      case WheelPress(:final button):
        if (_swallowPress && button == WheelButton.centre) {
          _swallowPress = false;
          return true;
        }
        switch (button) {
          case WheelButton.centre:
            _activate(_focus);
          case WheelButton.menu:
            if (_ratingEditing) {
              _ratingEditing = false;
            } else {
              _closePage2();
            }
          case WheelButton.playPause ||
              WheelButton.previous ||
              WheelButton.next:
            used = false; // the shell drives the player
        }
      case _:
        break;
    }
    if (mounted) setState(() {});
    _describe();
    return used;
  }

  void _activate(Page2Row row) {
    switch (row) {
      case Page2Row.rating:
        _ratingEditing = !_ratingEditing;
      case Page2Row.shuffle:
        ref.read(playbackChoicesProvider.notifier).toggleShuffle();
      case Page2Row.repeat:
        ref.read(playbackChoicesProvider.notifier).cycleRepeat();
      case Page2Row.queue:
        widget.onOpenQueue?.call();
    }
  }

  void _closePage2() {
    _page2 = false;
    _ratingEditing = false;
    _syncHolds();
  }

  // ---- the wheel's screen-reader value -----------------------------------

  void _describe({bool force = false}) {
    // A panel's text is the wheel's while it is up; a screen under another
    // says nothing.
    if (!mounted || !_claimed || !_isTop) return;
    if (ref.read(shellProvider).panel != null) return;
    // While scanning, the position changes every tick: say it four times a
    // second at most.
    final now = _now();
    if (!force && _npc.holding && now - _describedAt < 250) return;
    _describedAt = now;
    final playing = _state.isPlaying;
    final duration = _state.duration ?? Duration.zero;
    if (_page2) {
      _bus.semantics.value = WheelSemantics(
        label: _ratingEditing ? 'Rating' : 'Click wheel',
        value: _ratingEditing
            ? '${_rating()} of 5 stars'
            : switch (_focus) {
                Page2Row.rating => 'Rating, 1 of 4',
                Page2Row.shuffle => 'Shuffle, 2 of 4',
                Page2Row.repeat => 'Repeat, 3 of 4',
                Page2Row.queue => 'Queue, 4 of 4',
              },
        playing: playing,
      );
    } else if (_npc.mode == NowPlayingWheelMode.seek) {
      _bus.semantics.value = WheelSemantics(
        label: 'Seek',
        value: '${spokenClock(_shownPosition)} of ${spokenClock(duration)}',
        playing: playing,
      );
    } else {
      final v = _volume;
      _bus.semantics.value = WheelSemantics(
        label: 'Volume',
        value: v == null ? '' : '${v.percent} percent',
        playing: playing,
      );
    }
  }

  /// The stars of the track playing: the one the wheel last set while its
  /// writes are still going to the store (so quick turns build on each
  /// other, as the firmware's `p->stars` does), else the store's.
  int _rating() {
    final id = _state.current?.id;
    if (id == null) return 0;
    final pending = _pendingStars;
    if (pending != null && pending.$1 == id) return pending.$2;
    return ref.read(trackStatsProvider(id)).value?.stars ?? 0;
  }

  /// Saves [stars] for track [id], one write after another.
  void _rate(String id, int stars) {
    final store = ref.read(trackStatsStoreProvider);
    _pendingStars = (id, stars);
    _ratingWrites++;
    _ratingChain = _ratingChain
        .then((_) => store.rate(id, stars))
        .catchError((Object e) {
          debugPrint('neupod: rating of $id not saved: $e');
        })
        .whenComplete(() {
          if (--_ratingWrites == 0) {
            _pendingStars = null;
            if (mounted) setState(() {});
          }
        });
  }

  // ---- build -------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(playerStateProvider).value ?? _state;
    final choices = ref.watch(playbackChoicesProvider);
    final placement = ref.watch(ratingPlacementProvider);
    final item = state.current;
    if (item != null) ref.watch(trackStatsProvider(item.id));
    final art = ref.watch(playingArtProvider).value;

    final seek = _npc.mode == NowPlayingWheelMode.seek;
    final mode = _page2
        ? NowPlayingMode.page2
        : seek
        ? NowPlayingMode.seek
        : (_volumeShown ? NowPlayingMode.volume : NowPlayingMode.rest);

    final duration = state.duration ?? item?.duration;
    final queueLabel = state.index == null
        ? null
        : '${state.index! + 1} of ${state.queue.length}';
    final data = NowPlayingData(
      title: item?.title ?? 'Nothing playing',
      artist: item?.artist ?? '',
      album: item?.album,
      formatLine: formatLine(state.format),
      // In seek mode the times stay the playing position; the target is the
      // big amber time.
      position: _page2 || seek ? _position : _shownPosition,
      duration: duration,
      art: art == null ? null : CoverArt(art, decodeWidth: 210),
      playState: item == null
          ? PlayState.stopped
          : (state.isPlaying ? PlayState.playing : PlayState.paused),
      shuffle: choices.shuffle,
      repeat: choices.repeat,
      rating: _rating(),
      queueLabel: queueLabel,
      contextLabel: queueLabel == null
          ? null
          : 'track ${state.index! + 1} of ${state.queue.length}',
    );

    return ShellFrame(
      title: 'Now Playing',
      context: data.queueLabel,
      contextLabel: data.contextLabel,
      message: _message,
      shuffle: data.shuffle,
      repeat: data.repeat,
      backdrop: BackdropLayout.nowPlaying,
      body: NowPlayingView(
        withStatusBar: false,
        data: data,
        mode: mode,
        volume: _volume?.level,
        seekTarget: Duration(milliseconds: _npc.target),
        ratingPlacement: placement,
        page2Focus: _focus,
        ratingEditing: _ratingEditing,
        onArtTap: () {
          setState(() {
            if (_npc.mode == NowPlayingWheelMode.seek) _npc.menu();
            _openPage2(swallowPress: false);
          });
          _syncHolds();
          _describe(force: true);
        },
        onPage2Tap: (row) => setState(() {
          _focus = row;
          _activate(row);
        }),
      ),
    );
  }
}
