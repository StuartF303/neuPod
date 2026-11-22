// SPDX-License-Identifier: MPL-2.0

/// Providers for the Licences screen (built in phase 6). Phase 2 supplies the
/// data source for the bundled assets.
library;

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/ui/features/licences/licences_data.dart';

/// Each bundled asset with its licence text.
final bundledLicenceTextsProvider =
    FutureProvider<List<(BundledLicence, String)>>((ref) async {
      return [for (final l in bundledLicences) (l, await loadLicenceText(l))];
    });
