// SPDX-License-Identifier: MPL-2.0

// Enforces the dependency rule of decision 0006 by reading the import, export
// and part lines of every Dart file under lib/:
//
// - every file lives in main.dart, app/, domain/, data/, audio/ or ui/;
// - domain/ imports only an allowlist: dart: libraries other than dart:ui and
//   dart:io, domain/ itself, and a few pure-Dart packages (_domainPackages);
// - data/ and audio/ import only domain/ (and themselves) from this app, and
//   no Riverpod;
// - ui/ does not import data/ or audio/ (it reaches them through app/);
// - Riverpod appears only in app/, ui/ and main.dart;
// - imports and exports use package: URIs, so the checks above see every edge
//   (relative URIs are allowed only on part directives, e.g. drift's
//   `part 'x.g.dart';`).

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

final _directive = RegExp(
  r'''^\s*(import|export|part(?:\s+of)?)\s+['"]([^'"]+)['"]''',
  multiLine: true,
);

const _layers = {'main.dart', 'app', 'domain', 'data', 'audio', 'ui'};

/// The only packages domain/ may import besides dart: and itself.
const _domainPackages = [
  'package:meta/',
  'package:collection/',
  'package:async/',
];

const _riverpod = [
  'package:riverpod/',
  'package:flutter_riverpod/',
  'package:hooks_riverpod/',
];

String _layerOf(String libRelativePath) {
  final slash = libRelativePath.indexOf('/');
  return slash < 0 ? libRelativePath : libRelativePath.substring(0, slash);
}

String? _appLayerOf(String uri) {
  const prefix = 'package:neupod/';
  if (!uri.startsWith(prefix)) return null;
  return _layerOf(uri.substring(prefix.length));
}

bool _startsWithAny(String uri, List<String> prefixes) =>
    prefixes.any(uri.startsWith);

bool _domainAllows(String uri) {
  if (uri.startsWith('dart:')) {
    return !uri.startsWith('dart:ui') && uri != 'dart:io';
  }
  return uri.startsWith('package:neupod/domain/') ||
      _startsWithAny(uri, _domainPackages);
}

/// Returns the rule broken by a file in [layer], or null.
String? layerViolation(String layer) =>
    _layers.contains(layer) ? null : 'unknown layer "$layer"';

/// Returns the rule broken by a [kind] directive (import, export, part,
/// part of) with [uri] in a file of [layer], or null.
String? violation(String layer, String uri, {String kind = 'import'}) {
  final isRelative = !uri.contains(':');
  if (isRelative) {
    return kind.startsWith('part')
        ? null
        : 'relative $kind (use package:neupod/...)';
  }
  final target = _appLayerOf(uri);
  switch (layer) {
    case 'domain':
      if (!_domainAllows(uri)) return 'domain imports $uri (not allowlisted)';
    case 'data' || 'audio':
      if (_startsWithAny(uri, _riverpod)) return '$layer imports Riverpod';
      if (target != null && target != 'domain' && target != layer) {
        return '$layer imports $target/';
      }
    case 'ui':
      if (target == 'data' || target == 'audio') {
        return 'ui imports $target/ (go through app/providers.dart)';
      }
    case 'main.dart' || 'app':
      break;
    default:
      return layerViolation(layer);
  }
  return null;
}

void main() {
  test('lib/ follows the layering of decision 0006', () {
    final lib = Directory('lib');
    expect(lib.existsSync(), isTrue, reason: 'run from the package root');

    final files = lib
        .listSync(recursive: true)
        .whereType<File>()
        .where((f) => f.path.endsWith('.dart'))
        .map((f) => f.path.replaceAll(r'\', '/').substring('lib/'.length))
        .toList();
    expect(files, isNotEmpty, reason: 'no Dart files found under lib/');
    expect(
      files.where((f) => f.startsWith('domain/')),
      isNotEmpty,
      reason: 'no files found under lib/domain/: is the scan broken?',
    );

    final problems = <String>[];
    for (final relative in files) {
      final layer = _layerOf(relative);
      final misplaced = layerViolation(layer);
      if (misplaced != null) problems.add('lib/$relative: $misplaced');
      final source = File('lib/$relative').readAsStringSync();
      for (final match in _directive.allMatches(source)) {
        final kind = match.group(1)!.replaceAll(RegExp(r'\s+'), ' ');
        final uri = match.group(2)!;
        final rule = violation(layer, uri, kind: kind);
        if (rule != null) problems.add('lib/$relative: $uri: $rule');
      }
    }
    expect(problems, isEmpty, reason: problems.join('\n'));
  });

  test('the rule catches what it should', () {
    // domain: allowlist only.
    expect(violation('domain', 'package:flutter/widgets.dart'), isNotNull);
    expect(
      violation('domain', 'package:material_ui/material_ui.dart'),
      isNotNull,
    );
    expect(
      violation('domain', 'package:cupertino_ui/cupertino_ui.dart'),
      isNotNull,
    );
    expect(violation('domain', 'dart:ui'), isNotNull);
    expect(violation('domain', 'dart:ui_web'), isNotNull);
    expect(violation('domain', 'dart:io'), isNotNull);
    expect(violation('domain', 'package:neupod/data/db.dart'), isNotNull);
    expect(violation('domain', 'package:riverpod/riverpod.dart'), isNotNull);
    expect(violation('domain', 'dart:async'), isNull);
    expect(violation('domain', 'dart:typed_data'), isNull);
    expect(violation('domain', 'package:meta/meta.dart'), isNull);
    expect(violation('domain', 'package:collection/collection.dart'), isNull);
    expect(violation('domain', 'package:neupod/domain/x.dart'), isNull);

    // data and audio.
    expect(violation('data', 'package:neupod/ui/x.dart'), isNotNull);
    expect(violation('audio', 'package:neupod/app/providers.dart'), isNotNull);
    expect(violation('audio', 'package:flutter_riverpod/x.dart'), isNotNull);
    expect(violation('data', 'package:neupod/domain/x.dart'), isNull);

    // ui.
    expect(violation('ui', 'package:neupod/data/x.dart'), isNotNull);
    expect(violation('ui', 'package:neupod/app/providers.dart'), isNull);

    // Relative URIs: only on part directives.
    expect(violation('ui', '../x.dart'), isNotNull);
    expect(violation('data', 'x.dart', kind: 'export'), isNotNull);
    expect(violation('data', 'db.g.dart', kind: 'part'), isNull);
    expect(violation('data', 'db.dart', kind: 'part of'), isNull);

    // Unknown layers.
    expect(layerViolation('utils'), isNotNull);
    expect(layerViolation('helpers.dart'), isNotNull);
    expect(violation('utils', 'package:flutter/widgets.dart'), isNotNull);
    expect(layerViolation('main.dart'), isNull);
    expect(layerViolation('domain'), isNull);
  });
}
