// SPDX-License-Identifier: MPL-2.0

// The release check for Custom mode's board pictures (design README, Open
// question 2): a store build must not ship a placeholder picture.
//
//   cd mobile && dart run tool/check_release_boards.dart
//
// Exits 1 and names the boards while any entry of assets/boards/boards.json is
// still marked `"placeholder": true`, or when a board has no picture or frost
// copy. Run it before every store build (the app workflow runs it when started
// by hand with `release_check` on). A real picture of a neuPod board replaces
// a placeholder with `tool/build_board.dart`, and the entry loses its
// `placeholder` flag.

import 'dart:io';

import 'package:neupod/domain/style/board.dart';

/// What stops [boards] shipping: one line per problem, none when it may.
List<String> releaseProblems(
  List<BoardInfo> boards, {
  required bool Function(String path) exists,
}) => [
  if (boards.isEmpty) 'assets/boards/boards.json lists no board',
  for (final b in boards) ...[
    if (b.placeholder)
      '${b.id}: the picture is a PLACEHOLDER, not a neuPod board',
    if (!exists(b.picturePath)) '${b.id}: ${b.picturePath} is missing',
    if (!exists(b.frostPath)) '${b.id}: ${b.frostPath} is missing',
  ],
];

void main() {
  final file = File('assets/boards/boards.json');
  if (!file.existsSync()) {
    stderr.writeln('run from mobile/: assets/boards/boards.json not found');
    exit(2);
  }
  final problems = releaseProblems(
    parseBoards(file.readAsStringSync()),
    exists: (path) => File(path).existsSync(),
  );
  if (problems.isEmpty) {
    stdout.writeln('OK: no placeholder board picture');
    return;
  }
  for (final p in problems) {
    stderr.writeln('release check failed: $p');
  }
  stderr.writeln(
    'A store build needs the owner\'s real board pictures '
    '(docs/design/2026-11-25-app-custom-mode/README.md, Open question 2).',
  );
  exit(1);
}
