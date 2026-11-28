// SPDX-License-Identifier: MPL-2.0

/// Which style and mode the device's screens use, and the device's own
/// status (battery, output) the status bar shows.
///
/// Style is chosen in Settings (UI styles design, owner decision 1); mode
/// follows the phone unless Settings pins it. Classic Dark and Modern Light
/// are not designed (UI styles Open question 1): they fall back to the
/// style's designed mode (`NeupodStyles.resolve`), while the body follows the
/// mode as the device view design says.
library;

import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/art_providers.dart';
import 'package:neupod/app/custom_looks_provider.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/domain/style/custom_look.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/art_colours.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// The chosen style and the mode setting.
@immutable
class StyleChoice {
  /// A choice; [mode] defaults to Auto, which follows the phone.
  const StyleChoice({
    this.family = StyleFamily.classic,
    this.mode = ModeSetting.auto,
  });

  final StyleFamily family;
  final ModeSetting mode;

  StyleChoice copyWith({StyleFamily? family, ModeSetting? mode}) =>
      StyleChoice(family: family ?? this.family, mode: mode ?? this.mode);

  @override
  bool operator ==(Object other) =>
      other is StyleChoice && other.family == family && other.mode == mode;

  @override
  int get hashCode => Object.hash(family, mode);
}

/// The settings-file key that holds the mode.
const String modeSettingKey = 'mode';

/// Holds the [StyleChoice]. The mode is kept in the settings file; the style
/// is not stored yet.
class StyleChoiceNotifier extends Notifier<StyleChoice> {
  bool _changed = false;

  /// Completes when the stored mode has been read (and applied, unless the
  /// choice was changed meanwhile). Never completes with an error.
  Future<void> loaded = Future<void>.value();

  @override
  StyleChoice build() {
    loaded = _load();
    return const StyleChoice();
  }

  Future<void> _load() async {
    try {
      final stored = await ref.read(settingsStoreProvider).read();
      final mode = modeSettingFromName(stored[modeSettingKey]);
      if (!_changed && ref.mounted) state = state.copyWith(mode: mode);
    } on Object catch (e) {
      debugPrint('neupod: mode not loaded: $e');
    }
  }

  void set(StyleChoice choice) {
    _changed = true;
    final modeChanged = choice.mode != state.mode;
    state = choice;
    if (modeChanged) {
      unawaited(
        ref.read(settingsStoreProvider).put(modeSettingKey, choice.mode.name),
      );
    }
  }
}

/// The chosen style and mode.
final styleChoiceProvider = NotifierProvider<StyleChoiceNotifier, StyleChoice>(
  StyleChoiceNotifier.new,
);

/// The look the device is painted with: the look in use while Mode is Custom
/// and one is in use, otherwise null (Custom with no look is Auto).
final activeLookProvider = Provider<CustomLook?>((ref) {
  final mode = ref.watch(styleChoiceProvider.select((c) => c.mode));
  if (mode != ModeSetting.custom) return null;
  return ref.watch(customLooksProvider.select((l) => l.inUse));
});

/// The mode in force: pinned, the phone's, or in Custom the look's screen
/// ([look] is [activeLookProvider]'s value; none falls back to Auto).
StyleMode effectiveMode(
  StyleChoice choice,
  Brightness platform, {
  CustomLook? look,
}) => switch (pinnedMode(choice, look)) {
  final m? => m,
  null => StyleMode.fromBrightness(platform),
};

/// The mode the choice pins, or null to follow the phone.
StyleMode? pinnedMode(StyleChoice choice, CustomLook? look) =>
    switch (choice.mode) {
      ModeSetting.light => StyleMode.light,
      ModeSetting.dark => StyleMode.dark,
      ModeSetting.auto => null,
      ModeSetting.custom => switch (look?.screen) {
        LookScreen.light => StyleMode.light,
        LookScreen.dark => StyleMode.dark,
        LookScreen.auto || null => null,
      },
    };

/// The art of the song now loaded: its own picture, as the device's Now
/// Playing shows (with its album's placeholder), or null when nothing is
/// loaded.
final playingArtProvider = FutureProvider<ArtRef?>((ref) async {
  final id = ref.watch(playerStateProvider.select((s) => s.value?.current?.id));
  if (id == null) return null;
  final library = await ref.watch(libraryViewProvider.future);
  return (await library.song(id))?.art;
});

/// The colours Modern takes on screens that show no album of their own: the
/// art of the song playing (UI styles Open question 5 asks whether that
/// should be so everywhere; Cover Flow and an album follow the album on
/// screen, as drawn). Null: the ground alone.
final ambientPaletteProvider = Provider<ArtPalette?>((ref) {
  final art = ref.watch(playingArtProvider).value;
  if (art == null) return null;
  return ref.watch(artPaletteProvider(art)).value;
});

/// What the device's status bar shows that the player does not say. The
/// phone's real battery and route come with the platform plugin; until then
/// a full battery and no icons.
@immutable
class DeviceStatus {
  /// A status.
  const DeviceStatus({
    this.battery = const BatteryState(level: 1),
    this.output = OutputIcon.none,
    this.bluetooth = BluetoothIcon.none,
    this.busy = false,
  });

  final BatteryState battery;
  final OutputIcon output;
  final BluetoothIcon bluetooth;

  /// A background job is running (the busy dots).
  final bool busy;
}

/// The device's status (battery, output, Bluetooth).
final deviceStatusProvider = Provider<DeviceStatus>(
  (ref) => const DeviceStatus(),
);

/// The two colours of a placeholder cover for [seed], as the mock-ups' split
/// squares: a dark and a light (`linear-gradient(135deg, dark 50%, light
/// 50%)`).
(Color, Color) placeholderColours(int seed) =>
    _placeholderPairs[seed % _placeholderPairs.length];

const _placeholderPairs = <(Color, Color)>[
  (Color(0xff16324f), Color(0xffd9734e)),
  (Color(0xff5a3e1b), Color(0xffe9c46a)),
  (Color(0xff3a3d5c), Color(0xff7fb39b)),
  (Color(0xff24464f), Color(0xffe86f50)),
  (Color(0xff1c3558), Color(0xffa8dadc)),
  (Color(0xff6b5876), Color(0xffeaac8b)),
  (Color(0xff2a2d44), Color(0xff8e9bb5)),
  (Color(0xff4a2a35), Color(0xfff0b27a)),
];

/// The palette Modern takes from [art]: for a placeholder, its two colours;
/// for a picture, its art cache thumbnail decoded once and cached by the
/// provider.
final artPaletteProvider = FutureProvider.family<ArtPalette?, ArtRef>((
  ref,
  art,
) async {
  final path = art.sources.isEmpty
      ? art.path
      : await ref.watch(artFileProvider((art, ArtSize.thumb)).future);
  if (path != null) {
    try {
      final Uint8List bytes = await File(path).readAsBytes();
      final p = await paletteFromEncodedArt(bytes);
      if (p != null) return p;
    } on Object {
      // Fall through to the placeholder's colours.
    }
  }
  final (dark, light) = placeholderColours(art.seed);
  return ArtPalette(
    dominant: light.toARGB32() & 0xffffff,
    secondary: dark.toARGB32() & 0xffffff,
  );
});
