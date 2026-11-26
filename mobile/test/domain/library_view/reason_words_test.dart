// SPDX-License-Identifier: MPL-2.0

// The reasons on greyed rows are the device's: checked against the firmware
// sources they were taken from.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/domain/library/track.dart';
import 'package:neupod/domain/library_view/library_view.dart';
import 'package:neupod/domain/library_view/reason_words.dart';

String _source(String path) => File('../$path').readAsStringSync();

/// The `case X: return "...";` lines of the C function [name] in [text].
Map<String, String> _cases(String text, String name) {
  final start = text.indexOf(name);
  expect(start, isNonNegative, reason: '$name not found');
  final body = text.substring(start, text.indexOf('\n}\n', start));
  return {
    for (final m in RegExp(r'case (\w+): return "([^"]*)";').allMatches(body))
      m.group(1)!: m.group(2)!,
  };
}

void main() {
  test('every stored failure is the device damaged reason, as index_file '
      'and play_from_meta give it', () {
    // lib_build.c: a file starts at LIB_PLAY_DAMAGED (an f_open failure
    // keeps it), and META_E_READ / META_E_FORMAT / META_E_NO_AUDIO map to it.
    final build = _source('app/library/lib_build.c');
    expect(build, contains('play = LIB_PLAY_DAMAGED;'));
    expect(
      build,
      contains(
        'default: return LIB_PLAY_DAMAGED; '
        '// META_E_READ, META_E_FORMAT, META_E_NO_AUDIO',
      ),
    );
    for (final r in [
      UnreadableReason.cannotOpen,
      UnreadableReason.cannotRead,
      UnreadableReason.wrongFormat,
      UnreadableReason.damaged,
    ]) {
      expect(devicePlayReason(r), DevicePlayReason.damaged, reason: '$r');
      expect(unreadableReasonText(r, 'flac'), 'Damaged file', reason: '$r');
    }
  });

  test("the damaged wording is lib_cant_play_str's, without its prefix", () {
    final words = _cases(
      _source('app/runtime/library_service.c'),
      'const char *lib_cant_play_str',
    );
    final device = words['LIB_PLAY_DAMAGED']!;
    expect(device, "Can't play: damaged file");
    // ui_frame.c strips "Can't play: " for the skip notice; the row starts
    // with a capital, as every row does.
    final bare = device.substring("Can't play: ".length);
    expect(damagedText, '${bare[0].toUpperCase()}${bare.substring(1)}');
  });

  test('a file not downloaded is the app\'s own reason (owner decision 7)', () {
    expect(devicePlayReason(UnreadableReason.notDownloaded), isNull);
    expect(
      unreadableReasonText(UnreadableReason.notDownloaded, 'flac'),
      'Not downloaded',
    );
  });

  test('codec names are codec_name()\'s (ui_library.c)', () {
    final names = _cases(
      _source('app/ui/ui_library.c'),
      'static const char *codec_name',
    );
    expect(names, {
      'LIB_CODEC_WAV': 'WAV',
      'LIB_CODEC_FLAC': 'FLAC',
      'LIB_CODEC_MP3': 'MP3',
      'LIB_CODEC_ALAC': 'ALAC',
      'LIB_CODEC_AAC': 'AAC',
    });
    for (final MapEntry(:key, :value) in names.entries) {
      final codec = key.substring('LIB_CODEC_'.length).toLowerCase();
      expect(codecName(codec), value);
    }
    expect(codecName('opus'), 'Opus');
    expect(codecName('ogg'), 'Ogg');
  });

  test("a format the phone cannot decode reads as the spec's `Opus, can't "
      'play`', () {
    expect(cannotDecodeText('opus'), "Opus, can't play");
    expect(cannotDecodeText('aac'), "AAC, can't play");
  });

  test('iOS cannot decode Ogg yet (owner decision 5); Android greys none '
      'ahead (ALAC depends on the phone)', () {
    for (final c in ['ogg', 'oga', 'opus', 'OGG']) {
      expect(cannotDecode(PlayerPlatform.ios, c), isTrue, reason: c);
      expect(cannotDecode(PlayerPlatform.android, c), isFalse, reason: c);
    }
    for (final c in ['flac', 'mp3', 'aac', 'alac', 'wav']) {
      expect(cannotDecode(PlayerPlatform.ios, c), isFalse, reason: c);
      expect(cannotDecode(PlayerPlatform.android, c), isFalse, reason: c);
    }
  });

  test('a song not downloaded says Downloading once asked for, until then '
      'Not downloaded; other reasons are untouched', () {
    Song song(String? reason, {bool downloadable = false}) => Song(
      id: '1',
      title: 't',
      artist: 'a',
      album: 'b',
      albumId: 'b',
      uri: 'file:///m/t.flac',
      unplayableReason: reason,
      downloadable: downloadable,
    );
    final away = song(notDownloadedText, downloadable: true);
    expect(songRowReason(away, const {}), 'Not downloaded');
    expect(songRowReason(away, const {'file:///m/t.flac'}), 'Downloading');
    expect(
      songRowReason(away, const {'file:///m/other.flac'}),
      notDownloadedText,
    );
    expect(
      songRowReason(song(damagedText), const {'file:///m/t.flac'}),
      'Damaged file',
    );
    expect(songRowReason(song(null), const {'file:///m/t.flac'}), isNull);
    expect(
      unreadableReasonText(UnreadableReason.notDownloaded, 'flac'),
      notDownloadedText,
    );
  });
}
