// SPDX-License-Identifier: MPL-2.0

// The generated test library (docs/app/README.md, How it is tested): a tree in
// the owner's convention, Artist/Year Album/NN Title.ext, with the awkward
// cases the scanner must handle, built from the repository's own small audio
// fixtures (tests/assets/audio) and art fixtures (tests/assets/art). Nothing
// here is anyone's music, and the output is never committed.
//
// Output layout:
//
//   <out>/.neupod-test-library   marker: the folder may be replaced by --force
//   <out>/manifest.json          what each file is and what the app should show
//   <out>/library/...            the music folder to point the app at
//
// Use it from the command line (main.dart) or from a test (generate()).

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'src/tags.dart';

export 'src/tags.dart' show TagSet;

/// The name of the marker file in the output folder.
const markerName = '.neupod-test-library';

/// Where the fixtures come from.
class Fixtures {
  Fixtures(this.assetsDir);

  /// The repository's `tests/assets` folder.
  final Directory assetsDir;

  late final Uint8List flac16 = _read('audio/flac16_44k_st.flac');
  late final Uint8List flacMono = _read('audio/flac16_48k_mono.flac');
  late final Uint8List flac24 = _read('audio/flac24_96k_st.flac');
  late final Uint8List mp3 = _read('audio/mp3_44k_st.mp3');
  late final Uint8List alac = _read('audio/alac16_44k_st.m4a');
  late final Uint8List folderJpg = _read('art/q420_600x600.jpg');
  late final Uint8List coverJpg = _read('art/q444_128x128.jpg');
  late final Uint8List smallJpg = _read('art/q420_40x30.jpg');

  Uint8List _read(String rel) =>
      File('${assetsDir.path}/$rel').readAsBytesSync();

  /// Finds `tests/assets` by walking up from [start] (default: the current
  /// directory), so the tool works from the repository root or `mobile/`.
  static Fixtures locate([Directory? start]) {
    var dir = (start ?? Directory.current).absolute;
    while (true) {
      final candidate = Directory('${dir.path}/tests/assets/audio');
      if (candidate.existsSync()) {
        return Fixtures(Directory('${dir.path}/tests/assets'));
      }
      final parent = dir.parent;
      if (parent.path == dir.path) {
        throw StateError('tests/assets/audio not found above ${start?.path}');
      }
      dir = parent;
    }
  }
}

/// The audio a generated file is made from.
enum Source {
  flac16,
  flacMono,
  flac24,
  mp3,
  alac,

  /// An empty file.
  zeroBytes,

  /// Bytes that are no audio format at all.
  garbage,

  /// The first 20 bytes of a FLAC file: the magic and half a STREAMINFO.
  truncatedFlac,
}

/// What the app should show for a track. Null fields have no expectation.
class Expect {
  const Expect({
    required this.title,
    this.artist,
    this.performer,
    this.album,
    this.year,
    this.disc,
    this.track,
    this.compilation = false,
    this.playable = true,
  });

  final String title;

  /// The album artist as the Artists list shows it; null for a compilation.
  final String? artist;

  /// The track's own artist (shown on its row), when it differs.
  final String? performer;
  final String? album;
  final int? year;
  final int? disc;
  final int? track;
  final bool compilation;
  final bool playable;

  Map<String, Object?> toJson() => {
    'title': title,
    'artist': artist,
    'performer': performer ?? artist,
    'album': album,
    'year': year,
    'disc': disc,
    'track': track,
    'compilation': compilation,
    'playable': playable,
  };
}

/// One file of the generated library.
class GenFile {
  const GenFile(
    this.path,
    this.source, {
    required this.kase,
    this.tags,
    this.id3Version = 4,
    this.embedArt = false,
    this.expect,
    this.ambiguous,
  });

  /// Relative to the library folder, `/`-separated.
  final String path;
  final Source source;

  /// The case it exercises (a short id, see [cases]).
  final String kase;

  /// Null: no tags at all (FLAC without VORBIS_COMMENT, MP3 without ID3).
  final TagSet? tags;
  final int id3Version;
  final bool embedArt;

  /// Null for a file that is not a track.
  final Expect? expect;

  /// Set when the specification does not settle what the app should show;
  /// the text says what is open. Tests must not assert these.
  final String? ambiguous;

  Map<String, Object?> toJson() => {
    'path': path,
    'case': kase,
    'source': source.name,
    'tags': tags?.toJson(),
    'id3Version': source == Source.mp3 && tags != null ? id3Version : null,
    'embeddedArt': embedArt,
    'expect': expect?.toJson(),
    'ambiguous': ambiguous,
  };
}

/// A file that is not audio (art, notes, system junk).
class OtherFile {
  const OtherFile(this.path, this.kind, {required this.kase, this.ambiguous});
  final String path;

  /// folderJpg, coverJpg, smallJpg, text, appleDouble, dsStore, flac16
  final String kind;
  final String kase;
  final String? ambiguous;

  Map<String, Object?> toJson() => {
    'path': path,
    'case': kase,
    'kind': kind,
    'ambiguous': ambiguous,
  };
}

const _va = 'Various Artists';
const _ambPerField =
    'per-field fallback: the spec says "where tags are missing" but not '
    'whether one present tag (here the title) stops the folder fallback for '
    'the other fields';
const _ambLongDisc =
    'disc merging: the folder names differ before "(Disc n)" ("[Disc 1 of 2]" '
    'and "[Disc 2 of 2]"), so whether the two discs are one album is open';
const _ambRoot =
    'no folder convention to fall back on for a file at the library root';
const _ambExt =
    'which file extensions are audio: a .wma is neither a required format '
    'nor obviously "not music"';
const _ambArt =
    'art order when an album has embedded art, Folder.jpg and cover.jpg';

/// The curated cases. Every expectation follows docs/app/README.md; where the
/// specification is open the file carries [GenFile.ambiguous].
List<GenFile> get cases => [
  // Full FLAC tags, art in Folder.jpg.
  for (final (n, title, src) in [
    (1, 'Low Light', Source.flac16),
    (2, 'Salt Road', Source.flacMono),
    (3, 'Breakwater', Source.flac24),
  ])
    GenFile(
      'Pale Harbour/2004 Tidewater/0$n $title.flac',
      src,
      kase: 'full-tags-flac',
      tags: TagSet(
        title: title,
        artist: 'Pale Harbour',
        albumArtist: 'Pale Harbour',
        album: 'Tidewater',
        track: '$n/3',
        disc: '1/1',
        year: '2004',
        genre: 'Rock',
      ),
      expect: Expect(
        title: title,
        artist: 'Pale Harbour',
        album: 'Tidewater',
        year: 2004,
        disc: 1,
        track: n,
      ),
    ),
  // ID3v2.4 tags under a ", The" folder; art in cover.jpg.
  for (final (n, title) in [(1, 'Paper Moon'), (2, 'Neon Rain')])
    GenFile(
      'Lanterns, The/2011 Night Market/0$n $title.mp3',
      Source.mp3,
      kase: 'id3v24-the-folder',
      tags: TagSet(
        title: title,
        artist: 'The Lanterns',
        albumArtist: 'The Lanterns',
        album: 'Night Market',
        track: '$n',
        year: '2011',
        genre: 'Pop',
      ),
      expect: Expect(
        title: title,
        artist: 'The Lanterns',
        album: 'Night Market',
        year: 2011,
        track: n,
      ),
    ),
  // ", The" folder, no tags: the folder fallback shows "The Hollow Pines".
  for (final (n, title) in [(1, 'First Frost'), (2, 'Thaw')])
    GenFile(
      'Hollow Pines, The/1999 Cedar/0$n $title.flac',
      Source.flac16,
      kase: 'the-folder-no-tags',
      expect: Expect(
        title: title,
        artist: 'The Hollow Pines',
        album: 'Cedar',
        year: 1999,
        track: n,
      ),
    ),
  // "(Disc n)" in the album tag (and the folder), no disc number tag.
  for (final (d, n, title) in [
    (1, 1, 'Opening'),
    (1, 2, 'Middle Distance'),
    (2, 1, 'Return'),
  ])
    GenFile(
      'Kestrel Lane/2016 Long Division (Disc $d)/0$n $title.flac',
      Source.flacMono,
      kase: 'disc-in-album-tag',
      tags: TagSet(
        title: title,
        artist: 'Kestrel Lane',
        album: 'Long Division (Disc $d)',
        track: '$n',
        year: '2016',
      ),
      expect: Expect(
        title: title,
        artist: 'Kestrel Lane',
        // album: no expectation; the merged album's name is not specified.
        year: 2016,
        disc: d,
        track: n,
      ),
    ),
  // Disc number tags over two folders (the firmware's own case).
  for (final (folder, d, n, title) in [
    ('2005 Greatest Hits', 1, 1, 'Zulu'),
    ('2005 Greatest Hits', 1, 2, 'Zebra'),
    ('2005 Greatest Hits CD2', 2, 1, 'Zinc'),
  ])
    GenFile(
      'Zed Zone/$folder/0$n $title.flac',
      Source.flac16,
      kase: 'disc-tags-two-folders',
      tags: TagSet(
        title: title,
        artist: 'Zed Zone',
        album: 'Greatest Hits',
        track: '$n',
        disc: '$d/2',
        year: '2005',
      ),
      expect: Expect(
        title: title,
        artist: 'Zed Zone',
        album: 'Greatest Hits',
        year: 2005,
        disc: d,
        track: n,
      ),
    ),
  // The same album name by another artist is another album.
  GenFile(
    'Ember Field/2005 Greatest Hits/01 Spark.mp3',
    Source.mp3,
    kase: 'same-album-name-two-artists',
    tags: const TagSet(
      title: 'Spark',
      artist: 'Ember Field',
      album: 'Greatest Hits',
      track: '1',
      year: '2005',
    ),
    expect: const Expect(
      title: 'Spark',
      artist: 'Ember Field',
      album: 'Greatest Hits',
      year: 2005,
      track: 1,
    ),
  ),
  // "(Disc n)" in folder names only, no tags.
  for (final (d, n, title) in [
    (1, 1, 'Gear'),
    (1, 2, 'Piston'),
    (2, 1, 'Spring'),
  ])
    GenFile(
      'Quiet Engine/2015 Machines (Disc $d)/0$n $title.flac',
      Source.flacMono,
      kase: 'disc-folder-no-tags',
      expect: Expect(
        title: title,
        artist: 'Quiet Engine',
        // album: no expectation; the merged album's name is not specified.
        year: 2015,
        disc: d,
        track: n,
      ),
    ),
  // Compilation by album artist "Various Artists".
  for (final (n, title, performer) in [
    (1, 'Riptide', 'Pale Harbour'),
    (2, 'Ripple', 'Glass Fox'),
  ])
    GenFile(
      'Various Artists/2001 Summer Static/0$n $title.flac',
      Source.flac16,
      kase: 'compilation-various-artists',
      tags: TagSet(
        title: title,
        artist: performer,
        albumArtist: _va,
        album: 'Summer Static',
        track: '$n',
        year: '2001',
      ),
      expect: Expect(
        title: title,
        performer: performer,
        album: 'Summer Static',
        year: 2001,
        track: n,
        compilation: true,
      ),
    ),
  // Compilation by the flag (TCMP / COMPILATION), no album artist.
  GenFile(
    'Compilations/2003 Night Drive/01 Drift.mp3',
    Source.mp3,
    kase: 'compilation-flag',
    tags: const TagSet(
      title: 'Drift',
      artist: 'Neon Saint',
      album: 'Night Drive',
      track: '1',
      year: '2003',
      compilation: true,
    ),
    expect: const Expect(
      title: 'Drift',
      performer: 'Neon Saint',
      album: 'Night Drive',
      year: 2003,
      track: 1,
      compilation: true,
    ),
  ),
  GenFile(
    'Compilations/2003 Night Drive/02 Overpass.flac',
    Source.flac16,
    kase: 'compilation-flag',
    tags: const TagSet(
      title: 'Overpass',
      artist: 'Low Orbit',
      album: 'Night Drive',
      track: '2',
      year: '2003',
      compilation: true,
    ),
    expect: const Expect(
      title: 'Overpass',
      performer: 'Low Orbit',
      album: 'Night Drive',
      year: 2003,
      track: 2,
      compilation: true,
    ),
  ),
  // Very long folder names (the owner's example), no tags.
  for (final d in [1, 2])
    GenFile(
      'Ministry of Sound/2000 Ministry of Sound - the Annual IV - '
      '[Disc $d of 2] (Judge Jules) (Disc $d)/01 ${d == 1 ? 'Opening' : 'Closing'} Mix.flac',
      Source.flacMono,
      kase: 'long-names',
      expect: Expect(
        title: '${d == 1 ? 'Opening' : 'Closing'} Mix',
        artist: 'Ministry of Sound',
        year: 2000,
        disc: d,
        track: 1,
      ),
      ambiguous: _ambLongDisc,
    ),
  // A title longer than a file name can be: the tag holds all of it.
  GenFile(
    'Glass Fox/2018 Longform/01 ${_longTitle.substring(0, 180)}.mp3',
    Source.mp3,
    kase: 'long-title',
    tags: const TagSet(
      title: _longTitle,
      artist: 'Glass Fox',
      album: 'Longform',
      track: '1',
      year: '2018',
    ),
    expect: const Expect(
      title: _longTitle,
      artist: 'Glass Fox',
      album: 'Longform',
      year: 2018,
      track: 1,
    ),
  ),
  // Accents: FLAC (UTF-8), ID3v2.3 (UTF-16) and no tags.
  GenFile(
    'Sigrún Ósk/2010 Ljós/01 Þögn.flac',
    Source.flac16,
    kase: 'accents',
    tags: const TagSet(
      title: 'Þögn',
      artist: 'Sigrún Ósk',
      album: 'Ljós',
      track: '1',
      year: '2010',
    ),
    expect: const Expect(
      title: 'Þögn',
      artist: 'Sigrún Ósk',
      album: 'Ljós',
      year: 2010,
      track: 1,
    ),
  ),
  GenFile(
    'Motörbike/1995 Überfahrt/01 Straße.mp3',
    Source.mp3,
    kase: 'accents',
    id3Version: 3,
    tags: const TagSet(
      title: 'Straße',
      artist: 'Motörbike',
      album: 'Überfahrt',
      track: '1',
      year: '1995',
    ),
    expect: const Expect(
      title: 'Straße',
      artist: 'Motörbike',
      album: 'Überfahrt',
      year: 1995,
      track: 1,
    ),
  ),
  GenFile(
    'Émilie Côté/2007 Été/01 Ça va.flac',
    Source.flacMono,
    kase: 'accents',
    expect: const Expect(
      title: 'Ça va',
      artist: 'Émilie Côté',
      album: 'Été',
      year: 2007,
      track: 1,
    ),
  ),
  // Symbols and digits leading names.
  GenFile(
    '!Aktion/2012 ?Why Not/01 #1 Fan.flac',
    Source.flac16,
    kase: 'symbols-digits',
    tags: const TagSet(
      title: '#1 Fan',
      artist: '!Aktion',
      album: '?Why Not',
      track: '1',
      year: '2012',
    ),
    expect: const Expect(
      title: '#1 Fan',
      artist: '!Aktion',
      album: '?Why Not',
      year: 2012,
      track: 1,
    ),
  ),
  GenFile(
    '10 Tonne Truck/1988 99 Problems/01 7 Seconds.flac',
    Source.flacMono,
    kase: 'symbols-digits',
    expect: const Expect(
      title: '7 Seconds',
      artist: '10 Tonne Truck',
      album: '99 Problems',
      year: 1988,
      track: 1,
    ),
  ),
  GenFile(
    '+44 Minus/2006 & More/01 (Untitled).mp3',
    Source.mp3,
    kase: 'symbols-digits',
    tags: const TagSet(
      title: '(Untitled)',
      artist: '+44 Minus',
      album: '& More',
      track: '1',
      year: '2006',
    ),
    expect: const Expect(
      title: '(Untitled)',
      artist: '+44 Minus',
      album: '& More',
      year: 2006,
      track: 1,
    ),
  ),
  GenFile(
    'Year Zero/1982 1999/01 1999.flac',
    Source.flac16,
    kase: 'symbols-digits',
    expect: const Expect(
      title: '1999',
      artist: 'Year Zero',
      album: '1999',
      year: 1982,
      track: 1,
    ),
  ),
  // Articles and a sort tag.
  GenFile(
    'An Orchard/2019 A Quiet Year/01 The Apple.flac',
    Source.flac16,
    kase: 'articles',
    tags: const TagSet(
      title: 'The Apple',
      artist: 'An Orchard',
      album: 'A Quiet Year',
      track: '1',
      year: '2019',
    ),
    expect: const Expect(
      title: 'The Apple',
      artist: 'An Orchard',
      album: 'A Quiet Year',
      year: 2019,
      track: 1,
    ),
  ),
  GenFile(
    'Weird Owl/2014 Eat Its/01 Parody.flac',
    Source.flac16,
    kase: 'articles',
    tags: const TagSet(
      title: 'Parody',
      artist: 'Weird Owl',
      album: 'Eat Its',
      track: '1',
      year: '2014',
      artistSort: 'Owl, Weird',
    ),
    expect: const Expect(
      title: 'Parody',
      artist: 'Weird Owl',
      album: 'Eat Its',
      year: 2014,
      track: 1,
    ),
  ),
  // Missing tags: the folder convention fills everything in.
  GenFile(
    'Marrow/1977 Bone Dry/01 Dust.flac',
    Source.flac16,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Dust',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
      track: 1,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/02 Ash.mp3',
    Source.mp3,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Ash',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
      track: 2,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/Song Without Number.flac',
    Source.flacMono,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Song Without Number',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/04 Upper Case Extension.FLAC',
    Source.flac16,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Upper Case Extension',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
      track: 4,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/05 Mr. Blue Dot.flac',
    Source.flac16,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Mr. Blue Dot',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
      track: 5,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/06 Marrow Bones.m4a',
    Source.alac,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Marrow Bones',
      artist: 'Marrow',
      album: 'Bone Dry',
      year: 1977,
      track: 6,
    ),
  ),
  GenFile(
    'Marrow/Odds and Ends/03 Scrap.flac',
    Source.flac16,
    kase: 'missing-tags',
    expect: const Expect(
      title: 'Scrap',
      artist: 'Marrow',
      album: 'Odds and Ends',
      track: 3,
    ),
  ),
  GenFile(
    'Marrow/1977 Bone Dry/07 Folder Title.flac',
    Source.flac16,
    kase: 'partial-tags',
    tags: const TagSet(title: 'Tag Title'),
    expect: const Expect(title: 'Tag Title'),
    ambiguous: _ambPerField,
  ),
  // Unplayable files: listed greyed, never hidden.
  for (final (n, title, ext, src) in [
    (1, 'Zero Bytes', 'flac', Source.zeroBytes),
    (2, 'Not Audio', 'flac', Source.garbage),
    (3, 'Truncated', 'flac', Source.truncatedFlac),
    (4, 'Garbage', 'mp3', Source.garbage),
  ])
    GenFile(
      'Broken Things/2020 Damage/0$n $title.$ext',
      src,
      kase: 'unplayable',
      expect: Expect(
        title: title,
        artist: 'Broken Things',
        album: 'Damage',
        year: 2020,
        track: n,
        playable: false,
      ),
    ),
  GenFile(
    'Broken Things/2020 Damage/05 Old Format.wma',
    Source.garbage,
    kase: 'unplayable',
    ambiguous: _ambExt,
  ),
  // Embedded art next to Folder.jpg and cover.jpg.
  GenFile(
    'Pale Harbour/2006 Undertow EP/01 Undertow.flac',
    Source.flac16,
    kase: 'art-everywhere',
    embedArt: true,
    tags: const TagSet(
      title: 'Undertow',
      artist: 'Pale Harbour',
      albumArtist: 'Pale Harbour',
      album: 'Undertow EP',
      track: '1',
      year: '2006',
    ),
    expect: const Expect(
      title: 'Undertow',
      artist: 'Pale Harbour',
      album: 'Undertow EP',
      year: 2006,
      track: 1,
    ),
  ),
  // Files at the library root.
  GenFile(
    'Loose Track.flac',
    Source.flac16,
    kase: 'root-file',
    tags: const TagSet(
      title: 'Loose Track',
      artist: 'Nobody Band',
      album: 'Singles',
    ),
    expect: const Expect(
      title: 'Loose Track',
      artist: 'Nobody Band',
      album: 'Singles',
    ),
  ),
  GenFile(
    'Untagged At Root.mp3',
    Source.mp3,
    kase: 'root-file',
    ambiguous: _ambRoot,
  ),
  // Hidden and AppleDouble files: not tracks. The firmware skips every name
  // starting with '.' (app/library/lib_build.c), and the app follows it.
  GenFile(
    '.hidden/Secret.flac',
    Source.flac16,
    kase: 'hidden',
    tags: const TagSet(title: 'Secret', artist: 'Hidden'),
  ),
];

const _longTitle =
    'A Remarkably Long Title That Goes On and On Past Any Sensible Width, '
    'Through a Second Clause About Rivers, Harbours and Lanterns, Then a '
    'Third About Night Markets and Neon Rain, and Finally a Parenthetical '
    '(Extended Twelve-Inch Remix by Somebody With an Equally Long Name, Part '
    'One of Two)';

/// The other files of the curated library.
List<OtherFile> get otherFiles => const [
  OtherFile('Pale Harbour/2004 Tidewater/Folder.jpg', 'folderJpg', kase: 'art'),
  OtherFile(
    'Lanterns, The/2011 Night Market/cover.jpg',
    'coverJpg',
    kase: 'art',
  ),
  OtherFile(
    'Quiet Engine/2015 Machines (Disc 1)/Folder.jpg',
    'folderJpg',
    kase: 'art',
  ),
  OtherFile(
    'Pale Harbour/2006 Undertow EP/Folder.jpg',
    'folderJpg',
    kase: 'art-everywhere',
    ambiguous: _ambArt,
  ),
  OtherFile(
    'Pale Harbour/2006 Undertow EP/cover.jpg',
    'coverJpg',
    kase: 'art-everywhere',
    ambiguous: _ambArt,
  ),
  OtherFile('Pale Harbour/2004 Tidewater/notes.txt', 'text', kase: 'not-audio'),
  OtherFile(
    'Pale Harbour/2004 Tidewater/._01 Low Light.flac',
    'appleDouble',
    kase: 'hidden',
  ),
  OtherFile('Pale Harbour/.DS_Store', 'dsStore', kase: 'hidden'),
];

/// The result of a generation.
class GenResult {
  GenResult(this.outDir, this.trackFiles, this.otherFileCount, this.bytes);
  final Directory outDir;
  final int trackFiles;
  final int otherFileCount;
  final int bytes;
  Directory get library => Directory('${outDir.path}/library');
  File get manifest => File('${outDir.path}/manifest.json');
}

/// Writes the curated library (or, with [scaleTracks], the scale library of
/// that many tracks) into [outDir]. [outDir] must not exist, be empty, or
/// hold an earlier generated library (it has the marker); anything else is
/// refused, so the tool can never delete a real folder.
GenResult generate({
  required Directory outDir,
  Fixtures? fixtures,
  int? scaleTracks,
}) {
  final fx = fixtures ?? Fixtures.locate();
  _prepare(outDir);
  File('${outDir.path}/$markerName').writeAsStringSync(
    'Generated by mobile/tool/gen_test_library. Safe to delete.\n',
  );
  final lib = Directory('${outDir.path}/library')..createSync(recursive: true);
  final files = scaleTracks == null ? cases : scaleCases(scaleTracks);
  final others = scaleTracks == null
      ? otherFiles
      : scaleOtherFiles(scaleTracks);
  var bytes = 0;
  for (final f in files) {
    final data = render(f, fx);
    bytes += data.length;
    _write(lib, f.path, data);
  }
  for (final o in others) {
    final data = renderOther(o, fx);
    bytes += data.length;
    _write(lib, o.path, data);
  }
  final manifest = {
    'generator': 'mobile/tool/gen_test_library',
    'mode': scaleTracks == null ? 'cases' : 'scale',
    'library': 'library',
    if (scaleTracks == null) ...{
      'tracks': [for (final f in files) f.toJson()],
      'otherFiles': [for (final o in others) o.toJson()],
    } else ...{
      'trackCount': files.length,
      'albumCount': others.length,
    },
  };
  File('${outDir.path}/manifest.json')
      .writeAsStringSync(const JsonEncoder.withIndent('  ').convert(manifest));
  return GenResult(outDir, files.length, others.length, bytes);
}

/// The bytes of one generated file.
Uint8List render(GenFile f, Fixtures fx) {
  final art = f.embedArt ? fx.coverJpg : null;
  return switch (f.source) {
    Source.flac16 => flacWithTags(fx.flac16, tags: f.tags, pictureJpeg: art),
    Source.flacMono => flacWithTags(
      fx.flacMono,
      tags: f.tags,
      pictureJpeg: art,
    ),
    Source.flac24 => flacWithTags(fx.flac24, tags: f.tags, pictureJpeg: art),
    Source.mp3 => mp3WithTags(fx.mp3, tags: f.tags, version: f.id3Version),
    Source.alac => Uint8List.fromList(fx.alac),
    Source.zeroBytes => Uint8List(0),
    Source.garbage => _garbage(4096, _fnv1a(f.path)),
    Source.truncatedFlac => Uint8List.fromList(fx.flac16.sublist(0, 20)),
  };
}

/// The bytes of one non-track file.
Uint8List renderOther(OtherFile o, Fixtures fx) => switch (o.kind) {
  'folderJpg' => fx.folderJpg,
  'coverJpg' => fx.coverJpg,
  'smallJpg' => fx.smallJpg,
  'text' => Uint8List.fromList(utf8.encode('Not music.\n')),
  // AppleDouble header (magic 0x00051607, version 2), then zeros.
  'appleDouble' => Uint8List(4096)..setAll(0, [0, 5, 0x16, 7, 0, 2, 0, 0]),
  'dsStore' => Uint8List(64)..setAll(0, [0, 0, 0, 1, 0x42, 0x75, 0x64, 0x31]),
  _ => throw ArgumentError.value(o.kind),
};

/// FNV-1a over the UTF-8 bytes: a stable seed per path.
int _fnv1a(String s) {
  var h = 0x811C9DC5;
  for (final b in utf8.encode(s)) {
    h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF;
  }
  return h;
}

/// Deterministic noise (a 31-bit LCG) that is no audio format.
Uint8List _garbage(int n, int seed) {
  final out = Uint8List(n);
  var x = seed & 0x7FFFFFFF;
  for (var i = 0; i < n; i++) {
    x = (x * 1103515245 + 12345) & 0x7FFFFFFF;
    out[i] = (x >> 16) & 0xFF;
  }
  // Never start with a real magic number.
  out[0] = 0x00;
  return out;
}

// ---- scale mode -------------------------------------------------------------

const _first = [
  'Amber',
  'Basalt',
  'Copper',
  'Delta',
  'Echo',
  'Fable',
  'Granite',
  'Harbour',
  'Indigo',
  'Juniper',
  'Kestrel',
  'Lantern',
  'Meridian',
  'Nimbus',
  'Onyx',
  'Prism',
  'Quarry',
  'Russet',
  'Sable',
  'Tundra',
  'Umber',
  'Velvet',
  'Willow',
  'Xenon',
  'Yarrow',
  'Zephyr',
  'Ørsted',
  'Élan',
  'Ångström',
  '7th',
  '!Bang',
];
const _second = [
  'Lights',
  'Engine',
  'Choir',
  'Garden',
  'Signal',
  'Parade',
  'Static',
  'Harvest',
  'Atlas',
  'Circuit',
  'Orchard',
  'Tide',
  'Assembly',
  'Arcade',
];
const _words = [
  'Night',
  'River',
  'Glass',
  'Paper',
  'Iron',
  'Silver',
  'Hollow',
  'Bright',
  'Distant',
  'Quiet',
  'Electric',
  'Broken',
  'Golden',
  'Winter',
  'Summer',
  'Café',
  'Señor',
  'Zürich',
  '99',
  '#',
  'Ünder',
  'Fjörd',
];

/// [n] tracks: artists of 10 albums of 10 tracks, alternating FLAC and MP3,
/// tags on all but every 50th track (folder fallback), every 9th artist (from the second) a
/// ", The" folder, every 7th album in two discs.
List<GenFile> scaleCases(int n) {
  final out = <GenFile>[];
  for (var i = 0; i < n; i++) {
    final a = i ~/ 100; // artist index
    final al = (i ~/ 10) % 10; // album index within the artist
    final t = i % 10 + 1;
    final artist = _scaleArtist(a);
    final folderArtist = a % 9 == 1 ? '${artist.substring(4)}, The' : artist;
    final year = 1960 + (a * 7 + al * 3) % 66;
    final twoDiscs = (a * 10 + al) % 7 == 3;
    final disc = twoDiscs ? (t <= 5 ? 1 : 2) : null;
    final trackNo = twoDiscs ? (t <= 5 ? t : t - 5) : t;
    final album =
        '${_words[(a + al) % _words.length]} '
        '${_words[(a * 3 + al * 5 + 1) % _words.length]} ${al + 1}';
    final albumFolder = '$year $album${disc == null ? '' : ' (Disc $disc)'}';
    final title =
        '${_words[(i * 7) % _words.length]} '
        '${_words[(i * 11 + 3) % _words.length]} $i';
    final mp3 = i.isOdd;
    final ext = mp3 ? 'mp3' : 'flac';
    final tagged = i % 50 != 49;
    final nn = trackNo.toString().padLeft(2, '0');
    out.add(
      GenFile(
        '$folderArtist/$albumFolder/$nn $title.$ext',
        mp3 ? Source.mp3 : Source.flacMono,
        kase: 'scale',
        tags: tagged
            ? TagSet(
                title: title,
                artist: artist,
                albumArtist: artist,
                album: album,
                track: '$trackNo',
                disc: disc == null ? null : '$disc/2',
                year: '$year',
                genre: _words[a % _words.length],
              )
            : null,
      ),
    );
  }
  return out;
}

/// A small Folder.jpg in every album folder of the scale library.
List<OtherFile> scaleOtherFiles(int n) {
  final folders = <String>{};
  for (final f in scaleCases(n)) {
    folders.add(f.path.substring(0, f.path.lastIndexOf('/')));
  }
  return [
    for (final d in folders)
      OtherFile('$d/Folder.jpg', 'smallJpg', kase: 'scale'),
  ];
}

String _scaleArtist(int a) {
  final base =
      '${_first[a % _first.length]} ${_second[(a ~/ _first.length) % _second.length]}';
  final suffix = a >= _first.length * _second.length
      ? ' ${a ~/ (_first.length * _second.length) + 1}'
      : '';
  return a % 9 == 1 ? 'The $base$suffix' : '$base$suffix';
}

// ---- file system --------------------------------------------------------------

void _prepare(Directory outDir) {
  if (!outDir.existsSync()) {
    outDir.createSync(recursive: true);
    return;
  }
  final entries = outDir.listSync();
  if (entries.isEmpty) return;
  if (!File('${outDir.path}/$markerName').existsSync()) {
    throw StateError(
      '${outDir.path} is not empty and was not made by this tool; '
      'refusing to touch it',
    );
  }
  // Only what this tool writes; anything else in the folder stays.
  final lib = Directory('${outDir.path}/library');
  if (lib.existsSync()) lib.deleteSync(recursive: true);
  for (final name in ['manifest.json', markerName]) {
    final f = File('${outDir.path}/$name');
    if (f.existsSync()) f.deleteSync();
  }
}

void _write(Directory lib, String rel, Uint8List data) {
  final f = File('${lib.path}/$rel');
  f.parent.createSync(recursive: true);
  f.writeAsBytesSync(data, flush: false);
}
