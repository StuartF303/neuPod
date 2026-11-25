// SPDX-License-Identifier: MPL-2.0

/// Why a song cannot play, in the words at the right of its greyed row.
///
/// A song is unplayable in the app when the device would mark it damaged or
/// the phone's player cannot decode its format (spec, Formats; decision
/// 0001). The device's reasons are `lib_play_t` (`app/include/library.h`),
/// worded by `lib_cant_play_str` (`app/runtime/library_service.c`). The
/// scanner stores the app's own [UnreadableReason] codes; each maps to the
/// reason the device gives the same file.
library;

import 'package:neupod/domain/library/track.dart';

/// The device's reasons a track cannot play (`lib_play_t`, library.h), the
/// ones the phone has. The others (`LIB_PLAY_CHANNELS`, `_BITS`, `_RATE`,
/// `_BLOCK` and `_CODEC` for AAC) are limits of the device's decoders that the
/// phone's player does not share: a difference for a hardware reason
/// (constitution I), so those files play on the phone.
enum DevicePlayReason {
  /// `LIB_PLAY_DAMAGED`: a bad header, cut short, no audio, or a file that
  /// would not open (`index_file` starts every file at `LIB_PLAY_DAMAGED`;
  /// `play_from_meta` maps `META_E_READ`, `META_E_FORMAT`, `META_E_NO_AUDIO`
  /// to it).
  damaged,

  /// `LIB_PLAY_CODEC`: a format the player has no decoder for. On the phone,
  /// a format this platform's player cannot decode ([cannotDecode]).
  codec,
}

/// The device's reason for a file the scanner stored [reason] for, or null
/// for a reason the device does not have (an iCloud file not downloaded,
/// owner decision 7: the app's own).
///
/// Every failure to read a file is `LIB_PLAY_DAMAGED` on the device:
/// `index_file` (lib_build.c) leaves `play` at `LIB_PLAY_DAMAGED` when
/// `f_open` fails, and `play_from_meta` gives it for a read error, the wrong
/// format and no audio. So [UnreadableReason.cannotOpen],
/// [UnreadableReason.cannotRead] and [UnreadableReason.wrongFormat] are all
/// damaged, as [UnreadableReason.damaged] is. (The app still retries the
/// transient ones at the next scan; only the wording is the device's.)
DevicePlayReason? devicePlayReason(UnreadableReason reason) => switch (reason) {
  UnreadableReason.notDownloaded => null,
  UnreadableReason.cannotOpen ||
  UnreadableReason.cannotRead ||
  UnreadableReason.wrongFormat ||
  UnreadableReason.damaged => DevicePlayReason.damaged,
};

/// The words at the right of a greyed row for [reason]. [codec] is the
/// file's lower-case format name.
///
/// The device's damaged reason is its own words: `lib_cant_play_str` says
/// "Can't play: damaged file", and the reason without its prefix is what the
/// device shows when the queue skips the file (`ui_frame.c`). An unsupported
/// format is the UI styles design's `AAC, can't play` (and the spec's `Opus,
/// can't play`), with the device's codec names (`codec_name`, ui_library.c).
String unreadableReasonText(UnreadableReason reason, String codec) =>
    switch (devicePlayReason(reason)) {
      null => 'Not downloaded',
      DevicePlayReason.damaged => damagedText,
      DevicePlayReason.codec => cannotDecodeText(codec),
    };

/// The reason on the row of a damaged file.
const damagedText = 'Damaged file';

/// The reason on the row of a file whose format the phone cannot decode.
String cannotDecodeText(String codec) => "${codecName(codec)}, can't play";

/// A format's name as the device writes it (`codec_name` in ui_library.c:
/// `WAV`, `FLAC`, `MP3`, `ALAC`, `AAC`), with the app's Ogg formats, which the
/// device does not list, as the spec writes them (`Opus`).
String codecName(String codec) => switch (codec.toLowerCase()) {
  'wav' => 'WAV',
  'flac' => 'FLAC',
  'mp3' => 'MP3',
  'alac' => 'ALAC',
  'aac' || 'm4a' || 'mp4' => 'AAC',
  'ogg' || 'oga' || 'vorbis' => 'Ogg',
  'opus' => 'Opus',
  final other => other.toUpperCase(),
};

/// The phone platform whose player decides what can be decoded.
enum PlayerPlatform { android, ios }

/// Formats the phone's player cannot decode on [platform], by the codec name
/// the scanner stores (`Track.codec`).
///
/// iOS: Ogg Vorbis and Opus wait for the bundled Xiph decoders (owner
/// decision 5; `AudioEngine.swift` does not open them yet), so they are
/// greyed until those land. Android's Media3 decodes every format the
/// scanner lists except ALAC, which depends on the phone's own decoders, so
/// it is not greyed ahead: an ALAC file a phone cannot decode fails when it
/// starts, and the queue moves on (decision 0001).
Set<String> undecodableCodecs(PlayerPlatform platform) => switch (platform) {
  PlayerPlatform.ios => const {'ogg', 'oga', 'opus', 'vorbis'},
  PlayerPlatform.android => const {},
};

/// Whether the phone's player on [platform] cannot decode [codec].
bool cannotDecode(PlayerPlatform platform, String codec) =>
    undecodableCodecs(platform).contains(codec.toLowerCase());
