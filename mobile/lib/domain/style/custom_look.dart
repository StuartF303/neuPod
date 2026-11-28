// SPDX-License-Identifier: MPL-2.0

/// The Custom mode data model (design 2026-11-25-app-custom-mode, section
/// 10): a part's paint, a look, the saved looks, and the mode setting. Pure
/// Dart. Whole numbers, as the editor sets them; RGB, gradients, ink and the
/// flat flag are computed (`paintPart`), never stored.
///
/// Reading is tolerant: every number is clamped into its range, a seventh
/// look and beyond are dropped, nothing throws on a bad value.
library;

/// The three parts of the device a look paints.
enum LookPart {
  body(85),
  wheel(60),
  centre(60);

  const LookPart(this.maxTransparency);

  /// The most see-through the part may be, per cent.
  final int maxTransparency;
}

/// The screen's mode inside a look.
enum LookScreen { light, dark, auto }

/// Settings > Mode.
enum ModeSetting { light, dark, auto, custom }

/// How many looks may be saved.
const int maxLooks = 6;

/// The boards a look may name; an unknown id falls back to [defaultBoard].
const List<String> boardIds = ['main-top', 'main-bottom', 'copper'];

/// The board used when a look names none we know.
const String defaultBoard = 'main-top';

int _clampInt(Object? v, int lo, int hi, int fallback) {
  final n = switch (v) {
    int i => i,
    double d when d.isFinite => d.round(),
    _ => fallback,
  };
  return n < lo ? lo : (n > hi ? hi : n);
}

T _byName<T extends Enum>(List<T> values, Object? v, T fallback) {
  for (final e in values) {
    if (e.name == v) return e;
  }
  return fallback;
}

/// One part's paint: four whole numbers.
final class PartPaint {
  /// A paint; the numbers are used as given (see [clamped]).
  const PartPaint({
    required this.hue,
    required this.saturation,
    required this.brightness,
    this.transparency = 0,
  });

  /// 0..359, degrees round the colour wheel, red at 0.
  final int hue;

  /// 0..100, per cent.
  final int saturation;

  /// 0..100, per cent (the V of HSV).
  final int brightness;

  /// Per cent see-through: 0..85 for the body, 0..60 for the wheel and centre.
  final int transparency;

  /// This paint with every number inside its range for [part].
  PartPaint clamped(LookPart part) => PartPaint(
    hue: hue.clamp(0, 359),
    saturation: saturation.clamp(0, 100),
    brightness: brightness.clamp(0, 100),
    transparency: transparency.clamp(0, part.maxTransparency),
  );

  PartPaint copyWith({
    int? hue,
    int? saturation,
    int? brightness,
    int? transparency,
  }) => PartPaint(
    hue: hue ?? this.hue,
    saturation: saturation ?? this.saturation,
    brightness: brightness ?? this.brightness,
    transparency: transparency ?? this.transparency,
  );

  /// Reads [json], clamping into the ranges for [part]; anything unreadable
  /// becomes 0.
  factory PartPaint.fromJson(Object? json, LookPart part) {
    final Map<Object?, Object?> m = json is Map ? json : const {};
    return PartPaint(
      hue: _clampInt(m['hue'], 0, 359, 0),
      saturation: _clampInt(m['saturation'], 0, 100, 0),
      brightness: _clampInt(m['brightness'], 0, 100, 0),
      transparency: _clampInt(m['transparency'], 0, part.maxTransparency, 0),
    );
  }

  Map<String, Object?> toJson() => {
    'hue': hue,
    'saturation': saturation,
    'brightness': brightness,
    'transparency': transparency,
  };

  @override
  bool operator ==(Object other) =>
      other is PartPaint &&
      other.hue == hue &&
      other.saturation == saturation &&
      other.brightness == brightness &&
      other.transparency == transparency;

  @override
  int get hashCode => Object.hash(hue, saturation, brightness, transparency);

  @override
  String toString() =>
      'PartPaint($hue, $saturation, $brightness, $transparency)';
}

/// A saved look: three parts, a board and the screen's mode.
final class CustomLook {
  const CustomLook({
    required this.id,
    required this.name,
    required this.body,
    required this.wheel,
    required this.centre,
    this.board = defaultBoard,
    this.screen = LookScreen.auto,
  });

  /// Made once, never reused.
  final String id;

  /// 1 to 16 characters.
  final String name;

  final PartPaint body;
  final PartPaint wheel;
  final PartPaint centre;

  /// One of [boardIds].
  final String board;

  final LookScreen screen;

  /// The paint of [part].
  PartPaint paintOf(LookPart part) => switch (part) {
    LookPart.body => body,
    LookPart.wheel => wheel,
    LookPart.centre => centre,
  };

  CustomLook copyWith({
    String? id,
    String? name,
    PartPaint? body,
    PartPaint? wheel,
    PartPaint? centre,
    String? board,
    LookScreen? screen,
  }) => CustomLook(
    id: id ?? this.id,
    name: name ?? this.name,
    body: body ?? this.body,
    wheel: wheel ?? this.wheel,
    centre: centre ?? this.centre,
    board: board ?? this.board,
    screen: screen ?? this.screen,
  );

  /// Reads one look; null when [json] is not a map. [fallbackId] stands in
  /// for a missing id.
  static CustomLook? tryFromJson(Object? json, {required String fallbackId}) {
    if (json is! Map) return null;
    final rawId = json['id'];
    final id = rawId is String && rawId.isNotEmpty ? rawId : fallbackId;
    final rawName = json['name'];
    var name = rawName is String ? rawName.trim() : '';
    if (name.isEmpty) name = 'Look';
    if (name.length > 16) name = name.substring(0, 16);
    final rawBoard = json['board'];
    return CustomLook(
      id: id,
      name: name,
      body: PartPaint.fromJson(json['body'], LookPart.body),
      wheel: PartPaint.fromJson(json['wheel'], LookPart.wheel),
      centre: PartPaint.fromJson(json['centre'], LookPart.centre),
      board: boardIds.contains(rawBoard) ? rawBoard as String : defaultBoard,
      screen: _byName(LookScreen.values, json['screen'], LookScreen.auto),
    );
  }

  Map<String, Object?> toJson() => {
    'id': id,
    'name': name,
    'body': body.toJson(),
    'wheel': wheel.toJson(),
    'centre': centre.toJson(),
    'board': board,
    'screen': screen.name,
  };

  @override
  bool operator ==(Object other) =>
      other is CustomLook &&
      other.id == id &&
      other.name == name &&
      other.body == body &&
      other.wheel == wheel &&
      other.centre == centre &&
      other.board == board &&
      other.screen == screen;

  @override
  int get hashCode => Object.hash(id, name, body, wheel, centre, board, screen);

  @override
  String toString() => 'CustomLook($id, $name)';
}

/// The saved looks, in list order, and the one in use.
final class CustomLooks {
  /// [looks] at most [maxLooks]; [lookInUse] an id in [looks], or null.
  const CustomLooks({this.looks = const [], this.lookInUse});

  final List<CustomLook> looks;

  /// An id in [looks], or null. Kept when Mode leaves Custom.
  final String? lookInUse;

  /// The three starter looks (README section 7), Harbour in use.
  static const CustomLooks starters = CustomLooks(
    looks: [
      CustomLook(
        id: 'look-1',
        name: 'Harbour',
        body: PartPaint(hue: 205, saturation: 62, brightness: 44),
        wheel: PartPaint(hue: 40, saturation: 8, brightness: 94),
        centre: PartPaint(hue: 205, saturation: 62, brightness: 36),
      ),
      CustomLook(
        id: 'look-2',
        name: 'Sea glass',
        body: PartPaint(
          hue: 165,
          saturation: 35,
          brightness: 70,
          transparency: 55,
        ),
        wheel: PartPaint(hue: 165, saturation: 10, brightness: 96),
        centre: PartPaint(hue: 165, saturation: 40, brightness: 60),
      ),
      CustomLook(
        id: 'look-3',
        name: 'Clear case',
        body: PartPaint(
          hue: 200,
          saturation: 6,
          brightness: 92,
          transparency: 85,
        ),
        wheel: PartPaint(
          hue: 200,
          saturation: 4,
          brightness: 96,
          transparency: 60,
        ),
        centre: PartPaint(
          hue: 200,
          saturation: 6,
          brightness: 90,
          transparency: 60,
        ),
        board: 'copper',
      ),
    ],
    lookInUse: 'look-1',
  );

  /// The look in use, or null when none is.
  CustomLook? get inUse {
    final id = lookInUse;
    if (id == null) return null;
    for (final l in looks) {
      if (l.id == id) return l;
    }
    return null;
  }

  CustomLooks copyWith({
    List<CustomLook>? looks,
    String? Function()? lookInUse,
  }) => CustomLooks(
    looks: looks ?? this.looks,
    lookInUse: lookInUse == null ? this.lookInUse : lookInUse(),
  );

  /// Reads [json] (`{"looks": [...], "lookInUse": id}`); null when it is not
  /// that shape at all. A seventh look and beyond, and a repeated id, are
  /// dropped; a [lookInUse] not in the list becomes null.
  static CustomLooks? tryFromJson(Object? json) {
    if (json is! Map || json['looks'] is! List) return null;
    final looks = <CustomLook>[];
    final seen = <String>{};
    final raw = json['looks'] as List;
    for (var i = 0; i < raw.length && looks.length < maxLooks; i++) {
      final look = CustomLook.tryFromJson(raw[i], fallbackId: 'look-${i + 1}');
      if (look == null || !seen.add(look.id)) continue;
      looks.add(look);
    }
    final use = json['lookInUse'];
    return CustomLooks(
      looks: List.unmodifiable(looks),
      lookInUse: use is String && seen.contains(use) ? use : null,
    );
  }

  Map<String, Object?> toJson() => {
    'looks': [for (final l in looks) l.toJson()],
    'lookInUse': lookInUse,
  };

  @override
  bool operator ==(Object other) =>
      other is CustomLooks &&
      other.lookInUse == lookInUse &&
      other.looks.length == looks.length &&
      Iterable<int>.generate(looks.length)
          .every((i) => other.looks[i] == looks[i]);

  @override
  int get hashCode => Object.hash(lookInUse, Object.hashAll(looks));
}

/// Reads a stored mode name; unknown or missing is [ModeSetting.auto].
ModeSetting modeSettingFromName(Object? name) =>
    _byName(ModeSetting.values, name, ModeSetting.auto);
