// SPDX-License-Identifier: MPL-2.0

/// A look's name, typed with the wheel (design section 7: the device's text
/// entry, the letter ring, sixteen characters). The same ring and chips as
/// Search, over the same model (`WheelTextEntry`).
///
/// | | |
/// | --- | --- |
/// | wheel | turns the ring |
/// | centre | types the glyph |
/// | Previous, Next | along the mode chips |
/// | Menu | deletes one; on an empty field leaves, and the old name stays |
/// | Menu held | clears the field |
/// | Play/Pause | keeps the name and leaves |
///
/// An empty name is not accepted.
library;

import 'package:flutter/foundation.dart' show mapEquals;
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/domain/text_entry/wheel_text_entry.dart' as entry;
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/components/list_parts.dart';
import 'package:neupod/ui/components/search_entry.dart';
import 'package:neupod/ui/components/small_parts.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/custom/wheel_screen.dart';
import 'package:neupod/ui/screens/search/search_session.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/screens/shell/shell_state.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// The hint under the chips.
const String nameHint = 'Select: type · Menu: delete · Play: done';

/// The name entry.
class NameScreen extends ConsumerStatefulWidget {
  /// The name of the look with [lookId].
  const NameScreen({required this.lookId, super.key});

  final String lookId;

  @override
  ConsumerState<NameScreen> createState() => _NameScreenState();
}

class _NameScreenState extends ConsumerState<NameScreen>
    with WheelScreenState<NameScreen> {
  late final entry.WheelTextEntry _entry = entry.WheelTextEntry(
    initial: ref.read(customLooksProvider).byId(widget.lookId)?.name ?? '',
    maxChars: maxLookNameLength,
  );
  Map<WheelButton, int> _savedHolds = const {};
  Map<WheelButton, int> _ownHolds = const {};
  bool _claimed = false;

  List<String> get _ring {
    final n = _entry.stripLength();
    return [
      for (var o = -4; o <= 4; o++)
        if (n <= 1)
          (o == 0 ? _entry.stripKey(0).$2 : '')
        else
          _entry.stripKey(((_entry.sel + o) % n + n) % n).$2,
    ];
  }

  @override
  void becameTop() {
    if (_claimed) return;
    _claimed = true;
    _savedHolds = Map.of(wheelBus.holdMs);
    // Menu held 600 ms clears the field (`CLEAR_HOLD_MS`).
    wheelBus.holdMs[WheelButton.menu] = entry.kClearHoldTime.inMilliseconds;
    _ownHolds = Map.of(wheelBus.holdMs);
  }

  @override
  void leftTop() {
    if (!_claimed) return;
    _claimed = false;
    if (mapEquals(wheelBus.holdMs, _ownHolds)) {
      wheelBus.holdMs
        ..clear()
        ..addAll(_savedHolds);
    }
  }

  @override
  void describe() {
    if (!mounted || !isTop) return;
    if (ref.read(shellProvider).panel != null) return;
    final glyph = _entry.stripKey(_entry.sel).$2;
    wheelBus.semantics.value = WheelSemantics(
      label: 'Letter ring',
      value:
          '${SearchSession.chipLabel(_entry.chip)}, '
          '${glyph == ' ' ? 'space' : glyph}',
    );
  }

  void _changed() {
    setState(() {});
    describe();
  }

  @override
  bool onWheel(WheelEvent e) {
    final feedback = ref.read(deviceFeedbackProvider);
    switch (e) {
      case WheelStep(:final steps):
        _entry.event(entry.WheelEvent.scroll, steps);
        feedback.detent();
        _changed();
        return true;
      case WheelHoldStart(button: WheelButton.menu):
        _entry.clear();
        _changed();
        return true;
      case WheelPress(:final button):
        switch (button) {
          case WheelButton.centre:
            final before = _entry.text;
            _entry.event(entry.WheelEvent.select);
            if (_entry.text == before &&
                _entry.chars >= _entry.maxChars &&
                _entry.chip != entry.TextChip.delete) {
              feedback.endReached(); // full: the glyph was refused
            }
            _changed();
            return true;
          case WheelButton.previous:
            _entry.event(entry.WheelEvent.left);
            _changed();
            return true;
          case WheelButton.next:
            _entry.event(entry.WheelEvent.right);
            _changed();
            return true;
          case WheelButton.menu:
            // On an empty field Menu leaves (the shell goes back) and the
            // old name stays.
            if (_entry.text.isEmpty) return false;
            _entry.event(entry.WheelEvent.menu);
            _changed();
            return true;
          case WheelButton.playPause:
            _done();
            return true;
        }
      case _:
        return true;
    }
  }

  void _done() {
    if (!ref
        .read(customLookActionsProvider)
        .renameLook(widget.lookId, _entry.text)) {
      ref.read(deviceFeedbackProvider).endReached(); // empty: not accepted
      return;
    }
    ref.read(shellProvider.notifier).pop();
  }

  void _chip(int i) {
    final all = _entry.chips();
    if (i < 0 || i >= all.length) return;
    _entry
      ..chip = all[i]
      ..sel = 0;
    _changed();
  }

  @override
  Widget build(BuildContext context) {
    final look = ref.watch(customLooksProvider).byId(widget.lookId);
    return ShellFrame(
      title: 'Name',
      context: look?.name,
      body: Builder(
        builder: (context) {
          final k = NeupodTheme.of(context).tier.k;
          final chips = _entry.chips();
          return ListGround(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                SearchFieldBox(text: _entry.text, glyph: false),
                const Spacer(),
                LetterRing(glyphs: _ring),
                ModeChips(
                  labels: [for (final c in chips) SearchSession.chipLabel(c)],
                  active: chips.indexOf(_entry.chip),
                  onTap: _chip,
                ),
                SizedBox(height: 6 * k),
                const SearchHint(nameHint),
                SizedBox(height: 6 * k),
              ],
            ),
          );
        },
      ),
    );
  }
}
