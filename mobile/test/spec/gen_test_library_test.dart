// SPDX-License-Identifier: MPL-2.0

// Tests of the test-library generator itself (mobile/tool/gen_test_library).
// Not a spec suite: these run on every PR, with readers written here
// independently of the tool's writers.
@Tags(['tool'])
library;

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import '../../tool/gen_test_library/gen_test_library.dart';

/// FLAC metadata blocks: (type, body), and where the audio frames start.
(List<(int, Uint8List)>, int) _flacBlocks(Uint8List b) {
  expect(ascii.decode(b.sublist(0, 4)), 'fLaC');
  final blocks = <(int, Uint8List)>[];
  var p = 4;
  while (true) {
    final h = b[p];
    final len = (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3];
    blocks.add((h & 0x7F, Uint8List.sublistView(b, p + 4, p + 4 + len)));
    p += 4 + len;
    if (h & 0x80 != 0) return (blocks, p);
  }
}

Map<String, String> _vorbis(Uint8List body) {
  final d = ByteData.sublistView(body);
  var p = 0;
  String str() {
    final n = d.getUint32(p, Endian.little);
    p += 4;
    final s = utf8.decode(body.sublist(p, p + n));
    p += n;
    return s;
  }

  str(); // vendor
  final count = d.getUint32(p, Endian.little);
  p += 4;
  return {
    for (var i = 0; i < count; i++)
      for (final c in [str()])
        c.substring(0, c.indexOf('=')): c.substring(c.indexOf('=') + 1),
  };
}

/// ID3v2 text frames and the offset of the audio after the tag.
(int, Map<String, String>, int) _id3(Uint8List b) {
  expect(ascii.decode(b.sublist(0, 3)), 'ID3');
  final version = b[3];
  int syncsafe(int at) =>
      (b[at] << 21) | (b[at + 1] << 14) | (b[at + 2] << 7) | b[at + 3];
  final end = 10 + syncsafe(6);
  final frames = <String, String>{};
  var p = 10;
  while (p + 10 <= end && b[p] != 0) {
    final id = ascii.decode(b.sublist(p, p + 4));
    final size = version == 4
        ? syncsafe(p + 4)
        : ByteData.sublistView(b, p + 4, p + 8).getUint32(0);
    final data = b.sublist(p + 10, p + 10 + size);
    frames[id] = switch (data[0]) {
      3 => utf8.decode(data.sublist(1)),
      1 => () {
        expect(data.sublist(1, 3), [0xFF, 0xFE]);
        final units = <int>[
          for (var i = 3; i + 1 < data.length; i += 2)
            data[i] | (data[i + 1] << 8),
        ];
        return String.fromCharCodes(units);
      }(),
      _ => throw FormatException('encoding ${data[0]}'),
    };
    p += 10 + size;
  }
  return (version, frames, end);
}

void main() {
  late Directory tmp;
  late Fixtures fx;

  setUpAll(() => fx = Fixtures.locate());
  setUp(() => tmp = Directory.systemTemp.createTempSync('neupod_gen_'));
  tearDown(() => tmp.deleteSync(recursive: true));

  Uint8List read(GenResult g, String rel) =>
      File('${g.library.path}/$rel').readAsBytesSync();

  group('curated library', () {
    late GenResult g;
    late List<Map<String, Object?>> tracks;
    setUp(() {
      g = generate(outDir: Directory('${tmp.path}/out'), fixtures: fx);
      final m =
          jsonDecode(g.manifest.readAsStringSync()) as Map<String, Object?>;
      tracks = (m['tracks']! as List<Object?>).cast<Map<String, Object?>>();
    });

    test('writes every file of the manifest, the marker and nothing else', () {
      expect(File('${g.outDir.path}/$markerName').existsSync(), isTrue);
      final onDisk = g.library
          .listSync(recursive: true)
          .whereType<File>()
          .map((f) => f.path.substring(g.library.path.length + 1))
          .toSet();
      final listed = {
        for (final t in tracks) t['path']! as String,
        for (final o in otherFiles) o.path,
      };
      expect(onDisk, listed);
      expect(tracks, hasLength(cases.length));
    });

    test('covers every case the brief asks for', () {
      final kases = {for (final t in tracks) t['case']};
      expect(
        kases,
        containsAll([
          'full-tags-flac',
          'id3v24-the-folder',
          'the-folder-no-tags',
          'disc-in-album-tag',
          'disc-tags-two-folders',
          'disc-folder-no-tags',
          'compilation-various-artists',
          'compilation-flag',
          'long-names',
          'accents',
          'symbols-digits',
          'missing-tags',
          'unplayable',
        ]),
      );
    });

    test('every path follows Artist/Year Album/NN Title.ext or is flagged', () {
      final convention = RegExp(r'^[^/]+/(\d{4} )?[^/]+/(\d\d )?[^/]+\.\w+$');
      for (final t in tracks) {
        final path = t['path']! as String;
        if (t['ambiguous'] != null ||
            ['root-file', 'hidden'].contains(t['case'])) {
          continue;
        }
        expect(convention.hasMatch(path), isTrue, reason: path);
      }
    });

    test('no path component is over 255 bytes', () {
      for (final t in tracks) {
        for (final part in (t['path']! as String).split('/')) {
          expect(utf8.encode(part).length, lessThanOrEqualTo(255));
        }
      }
    });

    test('tagged FLAC: Vorbis comments as given, audio frames unchanged', () {
      final bytes = read(g, 'Pale Harbour/2004 Tidewater/01 Low Light.flac');
      final (blocks, audio) = _flacBlocks(bytes);
      expect(blocks.first.$1, 0, reason: 'STREAMINFO first');
      final vc = blocks.where((b) => b.$1 == 4).single.$2;
      expect(_vorbis(vc), {
        'TITLE': 'Low Light',
        'ARTIST': 'Pale Harbour',
        'ALBUMARTIST': 'Pale Harbour',
        'ALBUM': 'Tidewater',
        'TRACKNUMBER': '1/3',
        'DISCNUMBER': '1/1',
        'DATE': '2004',
        'GENRE': 'Rock',
      });
      final (_, fxAudio) = _flacBlocks(fx.flac16);
      expect(bytes.sublist(audio), fx.flac16.sublist(fxAudio));
      expect(blocks.first.$2, _flacBlocks(fx.flac16).$1.first.$2);
    });

    test('tagged FLAC: accents and the compilation flag', () {
      final sig = _flacBlocks(read(g, 'Sigrún Ósk/2010 Ljós/01 Þögn.flac'));
      expect(
        _vorbis(sig.$1.where((b) => b.$1 == 4).single.$2)['TITLE'],
        'Þögn',
      );
      final ov = _flacBlocks(
        read(g, 'Compilations/2003 Night Drive/02 Overpass.flac'),
      );
      expect(
        _vorbis(ov.$1.where((b) => b.$1 == 4).single.$2)['COMPILATION'],
        '1',
      );
    });

    test('untagged FLAC has no VORBIS_COMMENT block at all', () {
      final (blocks, _) = _flacBlocks(
        read(g, 'Marrow/1977 Bone Dry/01 Dust.flac'),
      );
      expect(blocks.where((b) => b.$1 == 4), isEmpty);
      expect(blocks.single.$1, 0);
    });

    test('embedded art: a front-cover JPEG PICTURE block', () {
      final (blocks, _) = _flacBlocks(
        read(g, 'Pale Harbour/2006 Undertow EP/01 Undertow.flac'),
      );
      final pic = blocks.where((b) => b.$1 == 6).single.$2;
      final d = ByteData.sublistView(pic);
      expect(d.getUint32(0), 3);
      expect(ascii.decode(pic.sublist(8, 18)), 'image/jpeg');
      expect(pic.sublist(pic.length - fx.coverJpg.length), fx.coverJpg);
    });

    test('ID3v2.4: UTF-8 frames, TDRC, audio unchanged', () {
      final bytes = read(
        g,
        'Lanterns, The/2011 Night Market/01 Paper Moon.mp3',
      );
      final (version, frames, end) = _id3(bytes);
      expect(version, 4);
      expect(frames, {
        'TIT2': 'Paper Moon',
        'TPE1': 'The Lanterns',
        'TPE2': 'The Lanterns',
        'TALB': 'Night Market',
        'TRCK': '1',
        'TDRC': '2011',
        'TCON': 'Pop',
      });
      expect(bytes.sublist(end), fx.mp3);
    });

    test('ID3v2.3: UTF-16 frames with accents, TYER', () {
      final (version, frames, _) = _id3(
        read(g, 'Motörbike/1995 Überfahrt/01 Straße.mp3'),
      );
      expect(version, 3);
      expect(frames['TIT2'], 'Straße');
      expect(frames['TPE1'], 'Motörbike');
      expect(frames['TALB'], 'Überfahrt');
      expect(frames['TYER'], '1995');
    });

    test('ID3v2.4 compilation flag (TCMP)', () {
      final (_, frames, _) = _id3(
        read(g, 'Compilations/2003 Night Drive/01 Drift.mp3'),
      );
      expect(frames['TCMP'], '1');
      expect(frames.containsKey('TPE2'), isFalse);
    });

    test('untagged MP3 and ALAC are the fixtures, byte for byte', () {
      expect(read(g, 'Marrow/1977 Bone Dry/02 Ash.mp3'), fx.mp3);
      expect(read(g, 'Marrow/1977 Bone Dry/06 Marrow Bones.m4a'), fx.alac);
    });

    test('unplayable files are what they say', () {
      expect(read(g, 'Broken Things/2020 Damage/01 Zero Bytes.flac'), isEmpty);
      expect(
        read(g, 'Broken Things/2020 Damage/03 Truncated.flac'),
        hasLength(20),
      );
      for (final rel in [
        'Broken Things/2020 Damage/02 Not Audio.flac',
        'Broken Things/2020 Damage/04 Garbage.mp3',
      ]) {
        final b = read(g, rel);
        expect(b, hasLength(4096));
        expect(b[0], 0, reason: 'no FLAC, ID3 or MPEG sync at the start');
      }
      for (final t in tracks.where((t) => t['case'] == 'unplayable')) {
        final e = t['expect'] as Map<String, Object?>?;
        if (e != null) expect(e['playable'], isFalse);
      }
    });

    test('generation is deterministic', () {
      final g2 = generate(outDir: Directory('${tmp.path}/out2'), fixtures: fx);
      for (final t in tracks) {
        final rel = t['path']! as String;
        expect(read(g2, rel), read(g, rel), reason: rel);
      }
      expect(g2.manifest.readAsStringSync(), g.manifest.readAsStringSync());
    });

    test('regenerates over its own output', () {
      final again = generate(outDir: g.outDir, fixtures: fx);
      expect(again.trackFiles, cases.length);
    });
  });

  test('regenerating deletes only what it wrote', () {
    final out = Directory('${tmp.path}/again');
    generate(outDir: out, fixtures: fx);
    final mine = File('${out.path}/notes-of-mine.txt')..writeAsStringSync('x');
    generate(outDir: out, fixtures: fx);
    expect(mine.readAsStringSync(), 'x');
  });

  test('refuses a non-empty folder it did not make', () {
    final dir = Directory('${tmp.path}/precious')..createSync();
    final keep = File('${dir.path}/keep.flac')..writeAsStringSync('mine');
    expect(
      () => generate(outDir: dir, fixtures: fx),
      throwsA(isA<StateError>()),
    );
    expect(keep.readAsStringSync(), 'mine');
  });

  group('scale library (250 tracks here; 20,000 from the command line)', () {
    late GenResult g;
    late List<String> files;
    setUp(() {
      g = generate(
        outDir: Directory('${tmp.path}/scale'),
        fixtures: fx,
        scaleTracks: 250,
      );
      files = g.library
          .listSync(recursive: true)
          .whereType<File>()
          .map((f) => f.path.substring(g.library.path.length + 1))
          .toList();
    });

    test('has exactly the tracks asked for, in the convention', () {
      final audio = files.where((f) => !f.endsWith('Folder.jpg')).toList();
      expect(audio, hasLength(250));
      final convention = RegExp(r'^[^/]+/\d{4} [^/]+/\d\d [^/]+\.(flac|mp3)$');
      for (final f in audio) {
        expect(convention.hasMatch(f), isTrue, reason: f);
      }
      final m =
          jsonDecode(g.manifest.readAsStringSync()) as Map<String, Object?>;
      expect(m['trackCount'], 250);
    });

    test('mixes formats, ", The" folders, two-disc albums and art', () {
      expect(files.where((f) => f.endsWith('.flac')), hasLength(125));
      expect(files.where((f) => f.endsWith('.mp3')), hasLength(125));
      expect(files.any((f) => f.split('/').first.endsWith(', The')), isTrue);
      expect(files.any((f) => f.contains(' (Disc 2)/')), isTrue);
      final albumDirs = {
        for (final f in files) f.substring(0, f.lastIndexOf('/')),
      };
      for (final d in albumDirs) {
        expect(files, contains('$d/Folder.jpg'));
      }
    });

    test('every 50th track is untagged, for the folder fallback', () {
      final untagged = scaleCases(250).where((c) => c.tags == null).length;
      expect(untagged, 5);
    });
  });
}
