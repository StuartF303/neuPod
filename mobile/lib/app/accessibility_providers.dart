// SPDX-License-Identifier: MPL-2.0

import 'package:flutter_riverpod/flutter_riverpod.dart';

/// The phone's Reduce Transparency setting (iOS; Android where the system
/// offers one). Flutter exposes no such flag (`MediaQuery` has
/// `highContrast` and `disableAnimations`, and nothing for transparency), so
/// this is the one place the app reads it from, and it reads false until the
/// transparency step (AP-1, 2/3) wires the platform flag in by overriding or
/// replacing this provider. Custom mode paints every part solid and loads no
/// board picture while it is true, and the part editor greys the Transparency
/// field.
final reduceTransparencyProvider = Provider<bool>((ref) => false);
