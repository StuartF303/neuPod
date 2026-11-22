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

import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/library_view_providers.dart';
import 'package:neupod/app/providers.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/style/art_palette.dart';
import 'package:neupod/ui/components/status_bar.dart';
import 'package:neupod/ui/style/art_colours.dart';
import 'package:neupod/ui/style/neupod_style.dart';

/// The chosen style and, when pinned, the mode.
@immutable
class StyleChoice {
  /// A choice; [mode] null follows the phone.
  const StyleChoice({this.family = StyleFamily.classic, this.mode});

  final StyleFamily family;
  final StyleMode? mode;

  StyleChoice copyWith({StyleFamily? family, StyleMode? Function()? mode}) =>
      StyleChoice(
        family: family ?? this.family,
        mode: mode == null ? this.mode : mode(),
      );

  @override
  bool operator ==(Object other) =>
      other is StyleChoice && other.family == family && other.mode == mode;

  @override
  int get hashCode => Object.hash(family, mode);
}

/// Holds the [StyleChoice].
class StyleChoiceNotifier extends Notifier<StyleChoice> {
  @override
  StyleChoice build() => const StyleChoice();

  void set(StyleChoice choice) => state = choice;
}

/// The chosen style and mode.
final styleChoiceProvider = NotifierProvider<StyleChoiceNotifier, StyleChoice>(
  StyleChoiceNotifier.new,
);

/// The mode in force: pinned, or the phone's.
StyleMode effectiveMode(StyleChoice choice, Brightness platform) =>
    choice.mode ?? StyleMode.fromBrightness(platform);

/// The album of the song now loaded, or null.
final playingAlbumProvider = FutureProvider<Album?>((ref) async {
  final id = ref.watch(playerStateProvider.select((s) => s.value?.current?.id));
  if (id == null) return null;
  final library = await ref.watch(libraryViewProvider.future);
  final song = await library.song(id);
  if (song == null) return null;
  return (await library.album(song.albumId))?.album;
});

/// The colours Modern takes on screens that show no album of their own: the
/// album of the song playing (UI styles Open question 5 asks whether that
/// should be so everywhere; Cover Flow and an album follow the album on
/// screen, as drawn). Null: the ground alone.
final ambientPaletteProvider = Provider<ArtPalette?>((ref) {
  final album = ref.watch(playingAlbumProvider).value;
  if (album == null) return null;
  return ref.watch(artPaletteProvider(album.art)).value;
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
  });

  final BatteryState battery;
  final OutputIcon output;
  final BluetoothIcon bluetooth;
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
/// for a cover file, decoded once and cached by the provider.
final artPaletteProvider = FutureProvider.family<ArtPalette?, ArtRef>((
  ref,
  art,
) async {
  final path = art.path;
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
