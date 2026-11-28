// SPDX-License-Identifier: MPL-2.0

/// The edits a person makes to the saved looks (design section 7): a new
/// look, a deleted one, a reset, a changed part. Pure Dart, on the immutable
/// [CustomLooks]; the app's actions (`app/custom_look_actions.dart`) save the
/// result.
library;

import 'package:neupod/domain/style/custom_look.dart';

/// The longest name a look has.
const int maxLookNameLength = 16;

/// The default finish's paint for each part, by what the look's screen is
/// (design section 7): the silver values for Light, the black ones for Dark.
/// Auto takes the one that suits the phone ([phoneIsLight]). All solid.
({PartPaint body, PartPaint wheel, PartPaint centre}) defaultPaints(
  LookScreen screen, {
  required bool phoneIsLight,
}) {
  final light = switch (screen) {
    LookScreen.light => true,
    LookScreen.dark => false,
    LookScreen.auto => phoneIsLight,
  };
  return light
      ? (
          body: const PartPaint(hue: 218, saturation: 4, brightness: 88),
          wheel: const PartPaint(hue: 220, saturation: 2, brightness: 95),
          centre: const PartPaint(hue: 220, saturation: 4, brightness: 87),
        )
      : (
          body: const PartPaint(hue: 225, saturation: 20, brightness: 8),
          wheel: const PartPaint(hue: 223, saturation: 12, brightness: 14),
          centre: const PartPaint(hue: 228, saturation: 19, brightness: 6),
        );
}

/// `Look n` with the lowest n not used by a name in [looks].
String nextLookName(Iterable<CustomLook> looks) {
  final used = {for (final l in looks) l.name};
  for (var n = 1; ; n++) {
    final name = 'Look $n';
    if (!used.contains(name)) return name;
  }
}

/// The number in a made id (`look-7` is 7), or 0 for any other id.
int lookIdNumber(String id) {
  final m = RegExp(r'^look-(\d+)$').firstMatch(id);
  return m == null ? 0 : int.parse(m.group(1)!);
}

/// The number for the next id: above [seq] (the highest ever made, kept in the
/// settings file) and above every made id in [looks].
int nextLookIdNumber(int seq, Iterable<CustomLook> looks) {
  var top = seq;
  for (final l in looks) {
    final n = lookIdNumber(l.id);
    if (n > top) top = n;
  }
  return top + 1;
}

/// Edits of the list of looks.
extension CustomLooksEdit on CustomLooks {
  /// Whether a seventh look would be refused.
  bool get full => looks.length >= maxLooks;

  /// The look with [id], or null.
  CustomLook? byId(String id) {
    for (final l in looks) {
      if (l.id == id) return l;
    }
    return null;
  }

  /// [look] at the end of the list and in use. Unchanged when the list is
  /// full.
  CustomLooks added(CustomLook look) =>
      full ? this : CustomLooks(looks: [...looks, look], lookInUse: look.id);

  /// [look] in place of the one with its id.
  CustomLooks replaced(CustomLook look) => CustomLooks(
    looks: [for (final l in looks) l.id == look.id ? look : l],
    lookInUse: lookInUse,
  );

  /// The look with [id] made in use.
  CustomLooks using(String id) =>
      byId(id) == null ? this : CustomLooks(looks: looks, lookInUse: id);

  /// Without the look with [id]. Deleting the one in use makes the one above
  /// it in use (the new first when it was first); none left, none in use.
  CustomLooks deleted(String id) {
    final at = looks.indexWhere((l) => l.id == id);
    if (at < 0) return this;
    final rest = [...looks]..removeAt(at);
    final String? use;
    if (lookInUse != id) {
      use = lookInUse;
    } else if (rest.isEmpty) {
      use = null;
    } else {
      use = rest[at > 0 ? at - 1 : 0].id;
    }
    return CustomLooks(looks: rest, lookInUse: use);
  }
}

/// [look] with its three parts at the default finish for its screen; the
/// name, the board and the screen are kept.
CustomLook resetLook(CustomLook look, {required bool phoneIsLight}) {
  final d = defaultPaints(look.screen, phoneIsLight: phoneIsLight);
  return look.copyWith(body: d.body, wheel: d.wheel, centre: d.centre);
}

/// [look] with the paint of [part] replaced.
CustomLook withPart(CustomLook look, LookPart part, PartPaint paint) =>
    switch (part) {
      LookPart.body => look.copyWith(body: paint),
      LookPart.wheel => look.copyWith(wheel: paint),
      LookPart.centre => look.copyWith(centre: paint),
    };

/// The editor's steps (design section 3): what one detent changes in each
/// field, and the field's range for [part]. Hue wraps; the others stop.
({int step, int max, bool wraps}) fieldRule(int field, LookPart part) =>
    switch (field) {
      0 => (step: 5, max: 359, wraps: true),
      1 => (step: 4, max: 100, wraps: false),
      2 => (step: 4, max: 100, wraps: false),
      _ => (step: 5, max: part.maxTransparency, wraps: false),
    };

int _get(PartPaint p, int field) => switch (field) {
  0 => p.hue,
  1 => p.saturation,
  2 => p.brightness,
  _ => p.transparency,
};

PartPaint _set(PartPaint p, int field, int v) => switch (field) {
  0 => p.copyWith(hue: v),
  1 => p.copyWith(saturation: v),
  2 => p.copyWith(brightness: v),
  _ => p.copyWith(transparency: v),
};

/// The value of [field] in [p].
int fieldValue(PartPaint p, int field) => _get(p, field);

/// [p] with [field] set to [v], clamped into the field's range for [part]
/// (hue wrapped).
PartPaint withField(PartPaint p, int field, int v, LookPart part) {
  final r = fieldRule(field, part);
  final n = r.wraps ? ((v % 360) + 360) % 360 : v.clamp(0, r.max);
  return _set(p, field, n);
}

/// [p] after the wheel turned [detents] (negative: back) on [field]. A value
/// off the step's grid first moves to the next line of the grid in that
/// direction, as a detent does. Hue wraps; the rest stop at their ends, where
/// the result equals [p].
PartPaint stepped(PartPaint p, int field, int detents, LookPart part) {
  if (detents == 0) return p;
  final r = fieldRule(field, part);
  final v = _get(p, field);
  final off = v % r.step;
  var target = v;
  var n = detents.abs();
  if (off != 0) {
    // The first detent lands on the grid.
    target = detents > 0 ? v + (r.step - off) : v - off;
    n--;
  }
  target += (detents > 0 ? 1 : -1) * n * r.step;
  return withField(p, field, target, part);
}
