// SPDX-License-Identifier: MPL-2.0

// Enforces the dependency rule of decision 0006 by reading the import, export
// and part lines of every Dart file under lib/:
//
// - domain/ imports nothing from this app, no Flutter, no dart:ui, no Riverpod;
// - data/ and audio/ import only domain/ from this app, and no Riverpod;
// - ui/ does not import data/ or audio/ (it reaches them through app/);
// - Riverpod appears only in app/, ui/ and main.dart;
// - lib/ uses package: imports only, so the checks above see every edge.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

final _directive = RegExp(
  r'''^\s*(?:import|export|part)\s+['"]([^'"]+)['"]''',
  multiLine: true,
);

const _riverpod = [
  'package:riverpod/',
  'package:flutter_riverpod/',
  'package:hooks_riverpod/',
];

const _flutter = ['package:flutter/', 'dart:ui'];

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

/// Returns the rule broken by [uri] in a file of [layer], or null.
String? violation(String layer, String uri) {
  if (!uri.contains(':')) {
    return 'relative import (use package:neupod/...)';
  }
  final target = _appLayerOf(uri);
  switch (layer) {
    case 'domain':
      if (_startsWithAny(uri, _flutter)) return 'domain imports Flutter';
      if (_startsWithAny(uri, _riverpod)) return 'domain imports Riverpod';
      if (target != null && target != 'domain') {
        return 'domain imports $target/';
      }
    case 'data' || 'audio':
      if (_startsWithAny(uri, _riverpod)) return '$layer imports Riverpod';
      if (target != null && target != 'domain' && target != layer) {
        return '$layer imports $target/';
      }
    case 'ui':
      if (target == 'data' || target == 'audio') {
        return 'ui imports $target/ (go through app/providers.dart)';
      }
  }
  return null;
}

void main() {
  test('lib/ follows the layering of decision 0006', () {
    final lib = Directory('lib');
    expect(lib.existsSync(), isTrue, reason: 'run from the package root');

    final problems = <String>[];
    final files = lib
        .listSync(recursive: true)
        .whereType<File>()
        .where((f) => f.path.endsWith('.dart'));
    for (final file in files) {
      final relative = file.path.replaceAll(r'\', '/').substring('lib/'.length);
      final layer = _layerOf(relative);
      for (final match in _directive.allMatches(file.readAsStringSync())) {
        final uri = match.group(1)!;
        final rule = violation(layer, uri);
        if (rule != null) problems.add('lib/$relative: $uri: $rule');
      }
    }
    expect(problems, isEmpty, reason: problems.join('\n'));
  });

  test('the rule catches what it should', () {
    expect(violation('domain', 'package:flutter/widgets.dart'), isNotNull);
    expect(violation('domain', 'dart:ui'), isNotNull);
    expect(violation('domain', 'package:neupod/data/db.dart'), isNotNull);
    expect(violation('domain', 'package:riverpod/riverpod.dart'), isNotNull);
    expect(violation('domain', 'dart:async'), isNull);
    expect(violation('domain', 'package:neupod/domain/x.dart'), isNull);
    expect(violation('data', 'package:neupod/ui/x.dart'), isNotNull);
    expect(violation('audio', 'package:neupod/app/providers.dart'), isNotNull);
    expect(violation('audio', 'package:flutter_riverpod/x.dart'), isNotNull);
    expect(violation('data', 'package:neupod/domain/x.dart'), isNull);
    expect(violation('ui', 'package:neupod/data/x.dart'), isNotNull);
    expect(violation('ui', 'package:neupod/app/providers.dart'), isNull);
    expect(violation('ui', '../x.dart'), isNotNull);
  });
}
