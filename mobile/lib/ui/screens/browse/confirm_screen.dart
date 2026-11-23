// SPDX-License-Identifier: MPL-2.0

/// A yes / no question, as the firmware asks one (`ui_confirm_create` in
/// `app/ui/ui_bluetooth.c`, used by Forget, Replace and Clear On-The-Go): a
/// screen with a large heading (`Forget Headset?`), a small line under it,
/// and the keys at the bottom, `Select = Forget  ·  Menu = Keep`. Select is
/// yes; Menu (the shell's own back) is no and leaves everything as it was.
/// Nothing else on the wheel does anything, and no row on it changes itself.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';

/// The keys line of the firmware's confirmation: `Select = Remove  ·  Menu =
/// Keep` (two spaces round the middle dot).
String confirmKeysLine(String yesWord, String noWord) =>
    'Select = $yesWord  ·  Menu = $noWord';

/// The question screen. Select pops it and then calls [onYes]; Menu is the
/// shell's back, which pops it and does nothing else.
class ConfirmScreen extends ConsumerStatefulWidget {
  /// A question titled [title] with [head] and [sub] and the two key words.
  const ConfirmScreen({
    required this.title,
    required this.head,
    required this.sub,
    required this.yesWord,
    required this.noWord,
    required this.onYes,
    super.key,
  });

  /// The status bar's title (the firmware's `title`).
  final String title;

  /// The large line (`Forget Headset?`).
  final String head;

  /// The small line under it.
  final String sub;

  /// What Select does, in one word.
  final String yesWord;

  /// What Menu does, in one word.
  final String noWord;

  /// Called after the screen has gone, when Select was pressed.
  final VoidCallback onYes;

  @override
  ConsumerState<ConfirmScreen> createState() => _ConfirmScreenState();
}

class _ConfirmScreenState extends ConsumerState<ConfirmScreen> {
  late final WheelBus _bus = ref.read(wheelBusProvider);
  ScreenHandle? _handle;
  bool _answered = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _handle) {
      _handle?.isTop.removeListener(_describe);
      if (_handle?.wheel == _onWheel) _handle?.wheel = null;
      _handle = handle;
      handle?.wheel = _onWheel;
      handle?.isTop.addListener(_describe);
    }
    WidgetsBinding.instance.addPostFrameCallback((_) => _describe());
  }

  @override
  void dispose() {
    _handle?.isTop.removeListener(_describe);
    if (_handle?.wheel == _onWheel) _handle?.wheel = null;
    super.dispose();
  }

  void _describe() {
    if (!mounted) return;
    if (_handle != null && !_handle!.isTop.value) return;
    _bus.semantics.value = WheelSemantics(
      label: widget.title,
      value:
          '${widget.head} ${widget.sub} '
          '${confirmKeysLine(widget.yesWord, widget.noWord)}',
      playing: _bus.semantics.value.playing,
    );
  }

  bool _onWheel(WheelEvent e) {
    switch (e) {
      case WheelPress(button: WheelButton.centre):
        if (!_answered) {
          _answered = true;
          ref.read(shellProvider.notifier).pop(); // destroys this screen
          widget.onYes();
        }
        return true;
      case WheelStep():
        return true; // a turn does nothing here
      case _:
        return false; // Menu goes back: no
    }
  }

  @override
  Widget build(BuildContext context) => ShellFrame(
    title: widget.title,
    body: Builder(
      builder: (context) {
        final theme = NeupodTheme.of(context);
        final type = NeupodType(theme.tier);
        final tokens = theme.style.tokens;
        final pad = 24 * theme.tier.k;
        return LayoutBuilder(
          builder: (context, c) => Padding(
            padding: EdgeInsets.fromLTRB(pad, 0, pad, pad),
            child: Column(
              children: [
                SizedBox(height: c.maxHeight * 0.22),
                Text(
                  widget.head,
                  textAlign: TextAlign.center,
                  style: type.large(tokens.ink),
                ),
                SizedBox(height: pad),
                Text(
                  widget.sub,
                  textAlign: TextAlign.center,
                  style: type.small(tokens.ink2),
                ),
                const Spacer(),
                Text(
                  confirmKeysLine(widget.yesWord, widget.noWord),
                  textAlign: TextAlign.center,
                  style: type.small(tokens.ink3),
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}
