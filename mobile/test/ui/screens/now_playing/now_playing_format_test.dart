// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/playback/player_state.dart';
import 'package:neupod/ui/screens/now_playing/now_playing_format.dart';

void main() {
  test('clock and remaining', () {
    expect(formatClock(const Duration(seconds: 92)), '1:32');
    expect(formatClock(const Duration(seconds: 5)), '0:05');
    expect(formatClock(const Duration(seconds: 3725)), '1:02:05');
    expect(formatClock(const Duration(seconds: -3)), '0:00');
    expect(
      formatRemaining(
        const Duration(seconds: 92),
        const Duration(minutes: 4, seconds: 1),
      ),
      '-2:29',
    );
    expect(
      formatRemaining(
        const Duration(seconds: 300),
        const Duration(seconds: 200),
      ),
      '-0:00',
    );
  });

  test('spoken clock', () {
    expect(spokenClock(const Duration(seconds: 92)), '1 minute 32 seconds');
    expect(spokenClock(const Duration(seconds: 61)), '1 minute 1 second');
    expect(spokenClock(Duration.zero), '0 seconds');
    expect(spokenClock(const Duration(seconds: 3600)), '1 hour');
  });

  test('the format line', () {
    expect(
      formatLine(
        const StreamFormat(codec: 'flac', sampleRate: 44100, bitsPerSample: 16),
      ),
      'FLAC 16/44.1',
    );
    expect(
      formatLine(
        const StreamFormat(codec: 'FLAC', sampleRate: 96000, bitsPerSample: 24),
      ),
      'FLAC 24/96',
    );
    expect(
      formatLine(const StreamFormat(codec: 'mp3', sampleRate: 48000)),
      'MP3 48',
    );
    expect(formatLine(const StreamFormat(codec: 'opus')), 'OPUS');
    expect(formatLine(const StreamFormat()), isNull);
    expect(formatLine(null), isNull);
  });
}
