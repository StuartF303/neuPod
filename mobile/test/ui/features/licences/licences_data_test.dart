// SPDX-License-Identifier: MPL-2.0

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:neupod/ui/features/licences/licences_data.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('every bundled licence text exists and is declared as an asset', () {
    final pubspec = File('pubspec.yaml').readAsStringSync();
    for (final l in bundledLicences) {
      expect(File(l.textAsset).existsSync(), isTrue, reason: l.textAsset);
      expect(pubspec, contains('- ${l.textAsset}'), reason: l.textAsset);
    }
  });

  test('the texts are the licences they claim', () async {
    final ofl = await loadLicenceText(bundledLicences[0]);
    expect(ofl, contains('SIL OPEN FONT LICENSE Version 1.1'));
    final lucide = await loadLicenceText(bundledLicences[1]);
    expect(lucide, contains('ISC License'));
    expect(lucide, contains('Cole Bemis')); // the Feather (MIT) part
  });

  test('LICENSE.md has a row for each bundled asset', () {
    final licence = File('../LICENSE.md').readAsStringSync();
    expect(licence, contains('mobile/assets/fonts'));
    expect(licence, contains('mobile/assets/icons'));
  });

  test('the assets reach the LicenseRegistry', () async {
    registerBundledLicences();
    final names = <String>{};
    await for (final e in LicenseRegistry.licenses) {
      names.addAll(e.packages);
    }
    expect(names, containsAll(['Montserrat', 'Lucide icons']));
  });
}
