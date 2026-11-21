// SPDX-License-Identifier: MPL-2.0

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/app/neupod_app.dart';

void main() {
  runApp(const ProviderScope(child: NeuPodApp()));
}
