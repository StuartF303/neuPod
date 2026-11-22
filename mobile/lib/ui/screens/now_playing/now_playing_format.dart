// SPDX-License-Identifier: MPL-2.0

/// The text Now Playing shows about a track: times and the format line.
library;

import 'package:neupod/domain/playback/player_state.dart';

/// `m:ss`, or `h:mm:ss` from an hour up.
String formatClock(Duration d) {
  final total = d.inSeconds < 0 ? 0 : d.inSeconds;
  final h = total ~/ 3600;
  final m = (total % 3600) ~/ 60;
  final s = total % 60;
  final ss = s.toString().padLeft(2, '0');
  return h > 0 ? '$h:${m.toString().padLeft(2, '0')}:$ss' : '$m:$ss';
}

/// The time left, with its minus (`-2:29`).
String formatRemaining(Duration position, Duration duration) {
  final left = duration - position;
  return '-${formatClock(left.isNegative ? Duration.zero : left)}';
}

/// A time for the screen reader (`1 minute 32 seconds`).
String spokenClock(Duration d) {
  final total = d.inSeconds < 0 ? 0 : d.inSeconds;
  final h = total ~/ 3600;
  final m = (total % 3600) ~/ 60;
  final s = total % 60;
  String unit(int n, String word) => '$n $word${n == 1 ? '' : 's'}';
  return [
    if (h > 0) unit(h, 'hour'),
    if (m > 0) unit(m, 'minute'),
    if (s > 0 || (h == 0 && m == 0)) unit(s, 'second'),
  ].join(' ');
}

/// A sample rate in kHz without a trailing zero (`44.1`, `48`, `96`).
String _khz(int hz) {
  final k = hz / 1000;
  final text = k.toStringAsFixed(1);
  return text.endsWith('.0') ? text.substring(0, text.length - 2) : text;
}

/// The small format line (`FLAC 16/44.1`, `MP3 44.1`): the codec in capitals,
/// the bit depth when the stream has one, and the sample rate in kHz. Null
/// when the platform reported nothing.
///
/// It is the stream's own rate, not what the output is doing: the
/// specification does not let the line claim bit-perfect output that has not
/// been measured.
String? formatLine(StreamFormat? format) {
  if (format == null) return null;
  final codec = format.codec?.trim();
  final bits = format.bitsPerSample;
  final rate = format.sampleRate;
  final parts = <String>[
    if (codec != null && codec.isNotEmpty) codec.toUpperCase(),
    if (bits != null && rate != null)
      '$bits/${_khz(rate)}'
    else if (rate != null)
      _khz(rate)
    else if (bits != null)
      '$bits bit',
  ];
  return parts.isEmpty ? null : parts.join(' ');
}
