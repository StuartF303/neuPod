// SPDX-License-Identifier: MPL-2.0

/// The part editor (design sections 2 and 3): the 4:3 screen shows the colour
/// wheel and four fields, and the real body, wheel and centre button round it
/// are the live preview. Every change repaints the device at once and is
/// saved.
///
/// | | in the editor |
/// | --- | --- |
/// | wheel | changes the focused field a detent at a time: Hue 5 (wraps), Saturation and Brightness 4, Transparency 5; the ends give the end haptic |
/// | Previous, Next | the field before and after |
/// | centre | the next part: Body, Wheel, Centre, Body |
/// | Menu | back to the look (the shell) |
/// | Play/Pause | play or pause (the shell) |
///
/// Direct touch: a drag on the colour wheel sets hue and saturation; a drag
/// on a bar sets that field; a tap on a field or a tab focuses it.
library;

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/accessibility_providers.dart';
import 'package:neupod/app/custom_look_actions.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/domain/style/board_list.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/domain/style/custom_paint.dart';
import 'package:neupod/domain/style/look_edit.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/device/device_feedback.dart';
import 'package:neupod/ui/device/edge_pulse.dart';
import 'package:neupod/ui/device/wheel_bus.dart';
import 'package:neupod/ui/screens/custom/part_picker_view.dart';
import 'package:neupod/ui/screens/custom/wheel_screen.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';

/// What the legend note says for [look]'s [part], given what is drawn: the
/// rule's result for the paint as it will be shown. [reduceTransparency] and
/// [highContrast] are the phone's settings; [field] is the focused one.
String legendNote(
  CustomLook look,
  LookPart part, {
  required int field,
  required bool reduceTransparency,
  required bool highContrast,
}) {
  if (field == 3 && highContrast) return 'Increase Contrast is on';
  if (field == 3 && reduceTransparency) return 'Reduce Transparency is on';
  if (part == LookPart.centre) return 'The centre button carries no legend';
  var p = look.paintOf(part).clamped(part);
  if (highContrast) {
    p = increaseContrastPaint(p, part);
  } else if (reduceTransparency) {
    p = p.copyWith(transparency: 0);
  }
  final r = paintPart(
    p,
    part,
    grounds: part == LookPart.wheel ? boardById(look.board).grounds : null,
    need: highContrast ? contrastTargetHigh : contrastTarget,
  );
  if (part == LookPart.wheel && r.transparencyKept < p.transparency) {
    return 'Held at ${r.transparencyKept} % so MENU stays readable';
  }
  final ink = r.inkIsBlack ? 'black' : 'white';
  return 'Legends: $ink ink, ${r.worst.toStringAsFixed(1)}:1'
      '${r.flat ? ' · painted flat' : ''}';
}

/// The part editor for one look.
class PartPickerScreen extends ConsumerStatefulWidget {
  /// The editor of the look with [lookId], opened on [part].
  const PartPickerScreen({
    required this.lookId,
    this.part = LookPart.body,
    super.key,
  });

  final String lookId;
  final LookPart part;

  @override
  ConsumerState<PartPickerScreen> createState() => _PartPickerScreenState();
}

class _PartPickerScreenState extends ConsumerState<PartPickerScreen>
    with WheelScreenState<PartPickerScreen>, SingleTickerProviderStateMixin {
  late LookPart _part = widget.part;
  int _field = 0;
  bool _atEnd = false;
  late final EdgePulseNotifier _pulse;
  late final AnimationController _anim;

  @override
  void initState() {
    super.initState();
    _pulse = ref.read(edgePulseProvider.notifier);
    _anim = AnimationController(vsync: this, duration: edgePulseDuration)
      ..addListener(_tick);
  }

  CustomLook? get _look => ref.read(customLooksProvider).byId(widget.lookId);

  bool get _highContrast => MediaQuery.highContrastOf(context);
  bool get _reduceTransparency => ref.read(reduceTransparencyProvider);
  bool get _transparencyLocked => _highContrast || _reduceTransparency;

  void _tick() {
    if (!mounted) return;
    _pulse.set(
      _anim.isCompleted || _anim.value == 0
          ? null
          : EdgePulse(_part, _anim.value),
    );
  }

  @override
  void leftTop() {
    // Never leave an edge lit behind.
    if (_anim.isAnimating) _anim.stop();
    final pulse = _pulse;
    Future.microtask(() {
      try {
        pulse.set(null);
      } on Object {
        // The app is gone.
      }
    });
  }

  @override
  void dispose() {
    _anim.dispose();
    super.dispose();
  }

  void _setPart(LookPart p) {
    if (p == _part) return;
    setState(() => _part = p);
    if (!MediaQuery.disableAnimationsOf(context)) _anim.forward(from: 0);
    describe();
  }

  void _setField(int f) {
    if (f == _field) return;
    setState(() => _field = f);
    _atEnd = false;
    describe();
  }

  void _save(PartPaint paint) {
    ref
        .read(customLookActionsProvider)
        .edit(widget.lookId, (l) => withPart(l, _part, paint));
  }

  @override
  void describe() {
    final look = _look;
    if (!mounted || !isTop || look == null) return;
    final p = look.paintOf(_part);
    String at(int detents) => fieldValueWords(
      _field,
      fieldValue(stepped(p, _field, detents, _part), _field),
    );
    final partWord = partName(_part).toLowerCase();
    wheelBus.semantics.value = WheelSemantics(
      label: fieldNames[_field],
      value: '${fieldValueWords(_field, fieldValue(p, _field))}, $partWord',
      increasedValue: '${at(1)}, $partWord',
      decreasedValue: '${at(-1)}, $partWord',
    );
  }

  void _step(int detents) {
    final look = _look;
    if (look == null) return;
    final feedback = ref.read(deviceFeedbackProvider);
    if (_field == 3 && _transparencyLocked) {
      if (!_atEnd) feedback.endReached();
      _atEnd = true;
      return;
    }
    final was = look.paintOf(_part).clamped(_part);
    final now = stepped(was, _field, detents, _part);
    if (now == was) {
      if (!_atEnd) feedback.endReached();
      _atEnd = true;
      return;
    }
    _atEnd = false;
    feedback.detent();
    _save(now);
    describe();
  }

  @override
  bool onWheel(WheelEvent e) {
    switch (e) {
      case WheelStep(:final steps):
        _step(steps);
        return true;
      case WheelPress(button: WheelButton.centre):
        _setPart(LookPart.values[(_part.index + 1) % LookPart.values.length]);
        return true;
      case WheelPress(button: WheelButton.previous):
        _setField((_field + 3) % 4);
        return true;
      case WheelPress(button: WheelButton.next):
        _setField((_field + 1) % 4);
        return true;
      case _:
        return false; // Menu goes back; Play/Pause plays
    }
  }

  @override
  Widget build(BuildContext context) {
    final look = ref.watch(customLooksProvider).byId(widget.lookId);
    final reduceT = ref.watch(reduceTransparencyProvider);
    final high = MediaQuery.highContrastOf(context);
    if (look == null) {
      return const ShellFrame(title: 'Custom look', body: SizedBox.shrink());
    }
    // Saying the wheel's value again after any change of the look.
    WidgetsBinding.instance.addPostFrameCallback((_) => describe());
    final paint = look.paintOf(_part).clamped(_part);
    return ShellFrame(
      title: partName(_part),
      context: look.name,
      body: PartPickerView(
        part: _part,
        paint: paint,
        field: _field,
        note: legendNote(
          look,
          _part,
          field: _field,
          reduceTransparency: reduceT,
          highContrast: high,
        ),
        transparencyLocked: reduceT || high,
        onPart: _setPart,
        onField: _setField,
        onValue: (f, v) {
          _setField(f);
          _save(withField(paint, f, v, _part));
        },
        onColour: (h, s) {
          _setField(0);
          _save(paint.copyWith(hue: h, saturation: s));
        },
      ),
    );
  }
}
