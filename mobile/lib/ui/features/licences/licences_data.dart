// SPDX-License-Identifier: MPL-2.0

/// The third-party assets bundled in the app, for the Licences screen
/// (specification, "Look and feel": "Every third-party asset and package is
/// listed on an in-app Licences screen"). Packages reach the screen through
/// Flutter's [LicenseRegistry], which collects their notices at build time;
/// assets do not, so they are listed here and added to the registry by
/// [registerBundledLicences]. Keep this list, `LICENSE.md` and the files under
/// `assets/` in step (`test/ui/features/licences/licences_data_test.dart`).
library;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

@immutable
final class BundledLicence {
  const BundledLicence({
    required this.name,
    required this.version,
    required this.spdx,
    required this.use,
    required this.textAsset,
  });

  final String name;
  final String version;

  /// SPDX identifier(s) of the licence.
  final String spdx;

  /// What the app uses it for.
  final String use;

  /// The licence text, bundled as an asset.
  final String textAsset;
}

const List<BundledLicence> bundledLicences = [
  BundledLicence(
    name: 'Montserrat',
    version: '9.000 (Medium)',
    spdx: 'OFL-1.1',
    use: 'The typeface of every screen.',
    textAsset: 'assets/fonts/OFL-Montserrat.txt',
  ),
  BundledLicence(
    name: 'Lucide icons',
    version: '1.72.0 (a subset of 14 icons)',
    spdx: 'ISC AND MIT',
    use:
        'Icons: chevron-right, check, headphones, bluetooth, '
        'bluetooth-connected, speaker, shuffle, repeat, repeat-1, volume, '
        'volume-2, lock, star and search. Some derive from Feather (MIT).',
    textAsset: 'assets/icons/lucide/LICENSE-Lucide.txt',
  ),
];

/// Reads the licence text of [licence].
Future<String> loadLicenceText(BundledLicence licence, {AssetBundle? bundle}) =>
    (bundle ?? rootBundle).loadString(licence.textAsset);

bool _registered = false;

/// Adds the bundled assets' licences to Flutter's [LicenseRegistry], next to
/// the packages'. Call once at start-up; later calls do nothing.
void registerBundledLicences({AssetBundle? bundle}) {
  if (_registered) return;
  _registered = true;
  LicenseRegistry.addLicense(() async* {
    for (final l in bundledLicences) {
      yield LicenseEntryWithLineBreaks([
        l.name,
      ], await loadLicenceText(l, bundle: bundle));
    }
  });
}
