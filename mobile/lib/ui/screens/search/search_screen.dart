// SPDX-License-Identifier: MPL-2.0

/// Search, typed with the wheel (owner decision 12): the wheel events drive a
/// [SearchSession]; the phone's keyboard never comes up.
library;

import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/domain/text_entry/wheel_text_entry.dart' as entry;
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/common/screen_frame.dart';
import 'package:neupod/ui/screens/panel/panel_layer.dart';
import 'package:neupod/ui/screens/search/search_session.dart';
import 'package:neupod/ui/screens/search/search_view.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// The library search Search runs after every edit. The shell overrides it
/// with an adapter over the library's read-only view (artists, albums and
/// songs, in the library's collation order); by default nothing is found.
final searchFunctionProvider = Provider<SearchFunction>(
  (ref) =>
      (query) => SearchResults.none,
);

/// The Search screen.
class SearchScreen extends ConsumerStatefulWidget {
  /// Search, left through [onBack]; a row opens through [onOpen].
  const SearchScreen({
    this.onBack,
    this.onOpen,
    this.initialText = '',
    this.style,
    super.key,
  });

  /// Menu on an empty field: leave Search.
  final VoidCallback? onBack;

  /// The centre on a result, or a tap on one.
  final void Function(SearchHit hit)? onOpen;

  /// Text to start with (kept when the shell rebuilds Search).
  final String initialText;

  /// The style; null takes the designed one for the phone's mode.
  final NeupodStyle? style;

  @override
  ConsumerState<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends ConsumerState<SearchScreen> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  late final SearchSession _session;
  StreamSubscription<WheelEvent>? _sub;
  bool _atEnd = false;
  late final Map<WheelButton, int> _savedHolds;

  @override
  void initState() {
    super.initState();
    _session = SearchSession(
      search: ref.read(searchFunctionProvider),
      initial: widget.initialText,
      onChange: _onChange,
    );
    _session.onLeave = () => widget.onBack?.call();
    _session.onOpen = (hit) => widget.onOpen?.call(hit);
    _savedHolds = Map.of(_bus.holdMs);
    // Menu held 600 ms clears the field (`CLEAR_HOLD_MS`).
    _bus.holdMs[WheelButton.menu] = entry.kClearHoldTime.inMilliseconds;
    _bus.atRoot.value = false;
    _sub = _bus.events.listen(_onEvent);
    WidgetsBinding.instance.addPostFrameCallback((_) => _describe());
  }

  @override
  void dispose() {
    _sub?.cancel();
    _bus.holdMs
      ..clear()
      ..addAll(_savedHolds);
    super.dispose();
  }

  void _onChange() {
    if (mounted) setState(() {});
    _describe();
  }

  void _describe() {
    if (!mounted) return;
    final typing = _session.phase == SearchPhase.typing;
    final player = ref.read(playerProvider).state;
    if (typing) {
      _bus.semantics.value = WheelSemantics(
        label: 'Letter ring',
        value: _glyphWords(_session.currentGlyph),
        playing: player.isPlaying,
      );
    } else {
      final all = _session.flatResults;
      final i = _session.selected;
      _bus.semantics.value = WheelSemantics(
        value: i < all.length
            ? '${all[i].title}, ${i + 1} of ${all.length}'
            : '',
        playing: player.isPlaying,
      );
    }
  }

  static String _glyphWords(String g) => switch (g) {
    ' ' => 'space',
    '' => '',
    _ => g,
  };

  void _onEvent(WheelEvent e) {
    if (ref.read(panelProvider) != null) return;
    final feedback = ref.read(deviceFeedbackProvider);
    switch (e) {
      case WheelStep(:final steps):
        final moved = _session.wheel(steps);
        if (_session.phase == SearchPhase.results) {
          if (moved) {
            _atEnd = false;
            feedback.detent();
          } else if (!_atEnd) {
            _atEnd = true;
            feedback.endReached();
          }
        } else {
          feedback.detent();
        }
      case WheelPress(:final button):
        _onPress(button);
      case WheelHoldStart(button: WheelButton.menu):
        _session.menuHeld();
      case _:
        break;
    }
  }

  void _onPress(WheelButton button) {
    final player = ref.read(playerProvider);
    switch (button) {
      case WheelButton.centre:
        _session.centre();
      case WheelButton.previous:
        if (_session.phase == SearchPhase.typing) {
          _session.previous();
        } else {
          player.skipToPrevious();
        }
      case WheelButton.next:
        if (_session.phase == SearchPhase.typing) {
          _session.next();
        } else {
          player.skipToNext();
        }
      case WheelButton.menu:
        _session.menu();
      case WheelButton.playPause:
        if (!_session.playPause()) {
          if (player.state.playWhenReady) {
            player.pause();
          } else {
            player.play();
          }
        }
    }
  }

  @override
  Widget build(BuildContext context) {
    final state = ref.watch(playerStateProvider).value ?? PlayerState.empty;
    return ScreenFrame(
      style: widget.style,
      child: SearchView(
        session: _session,
        playState: state.current == null
            ? PlayState.stopped
            : (state.isPlaying ? PlayState.playing : PlayState.paused),
        onOpen: widget.onOpen,
      ),
    );
  }
}
