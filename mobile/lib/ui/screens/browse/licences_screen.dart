// SPDX-License-Identifier: MPL-2.0

/// Licences, the last row of Settings (specification, "Settings": "About;
/// Licences"; owner decision 13). One row for each bundled third-party asset
/// (`bundledLicences`: Montserrat, Lucide) and one for the Dart and Flutter
/// packages, whose notices Flutter collects in `LicenseRegistry`. Select opens
/// the licence text, which the wheel scrolls; Menu goes back.
library;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:neupod/domain/wheel/wheel_events.dart';
import 'package:neupod/ui/features/licences/licences_data.dart';
import 'package:neupod/ui/screens/browse/browse_common.dart';
import 'package:neupod/ui/screens/shell/neupod_list.dart';
import 'package:neupod/ui/screens/shell/screen_scope.dart';
import 'package:neupod/ui/screens/shell/shell_frame.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';

/// The row for the packages Flutter's registry collects.
const String packagesLicenceTitle = 'Dart and Flutter packages';

/// The notices of every package in the registry, as one text: each package's
/// name, then its licence paragraphs. Bundled assets are skipped (they have
/// their own rows). Returns what could be read.
Future<String> packageLicenceText() async {
  final bundled = {for (final l in bundledLicences) l.name};
  final out = StringBuffer();
  try {
    await for (final entry in LicenseRegistry.licenses) {
      if (entry.packages.every(bundled.contains)) continue;
      out
        ..writeln(entry.packages.join(', '))
        ..writeln();
      for (final p in entry.paragraphs) {
        out.writeln(p.text);
      }
      out.writeln();
    }
  } on Object catch (e) {
    debugPrint('licence registry: $e');
  }
  final s = out.toString().trim();
  return s.isEmpty ? 'No package notices were found.' : s;
}

/// Licences.
class LicencesScreen extends ConsumerWidget {
  /// Licences.
  const LicencesScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) => ShellFrame(
    title: 'Licences',
    body: BrowseListBody(
      child: Builder(
        builder: (context) {
          final h = NeupodTheme.of(context).tier.rowHeight;
          return NeupodList(
            indexed: false,
            items: [
              for (final l in bundledLicences)
                NeupodListItem.row(
                  height: h,
                  title: l.name,
                  value: l.spdx,
                  onActivate: () => pushScreen(
                    ref,
                    l.name,
                    (_) => LicenceTextScreen(
                      title: l.name,
                      intro: '${l.name} ${l.version}\n${l.use}',
                      text: loadLicenceText(l),
                    ),
                  ),
                ),
              NeupodListItem.row(
                height: h,
                title: packagesLicenceTitle,
                onActivate: () => pushScreen(
                  ref,
                  packagesLicenceTitle,
                  (_) => LicenceTextScreen(
                    title: packagesLicenceTitle,
                    text: packageLicenceText(),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    ),
  );
}

/// One licence's text, scrolled by the wheel.
class LicenceTextScreen extends StatefulWidget {
  /// A text screen; [text] is loaded when the screen opens.
  const LicenceTextScreen({
    required this.title,
    required this.text,
    this.intro,
    super.key,
  });

  final String title;
  final String? intro;
  final Future<String> text;

  @override
  State<LicenceTextScreen> createState() => _LicenceTextScreenState();
}

class _LicenceTextScreenState extends State<LicenceTextScreen> {
  final _scroll = ScrollController();
  ScreenHandle? _handle;
  double _line = 29;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final handle = ScreenScope.maybeOf(context);
    if (handle != _handle) {
      if (_handle?.wheel == _onWheel) _handle?.wheel = null;
      _handle = handle;
      handle?.wheel = _onWheel;
    }
  }

  @override
  void dispose() {
    if (_handle?.wheel == _onWheel) _handle?.wheel = null;
    _scroll.dispose();
    super.dispose();
  }

  bool _onWheel(WheelEvent e) {
    if (e is! WheelStep || !_scroll.hasClients) return false;
    final line = _line;
    final pos = _scroll.position;
    _scroll.jumpTo(
      (pos.pixels + e.steps * line * 2).clamp(0.0, pos.maxScrollExtent),
    );
    return true;
  }

  @override
  Widget build(BuildContext context) => ShellFrame(
    title: widget.title,
    body: Builder(
      builder: (context) {
        final theme = NeupodTheme.of(context);
        _line = 29 * theme.tier.k;
        final style = NeupodType(theme.tier).small(theme.style.tokens.ink);
        return FutureBuilder<String>(
          future: widget.text,
          builder: (context, snap) {
            final body = snap.hasError
                ? 'The licence text could not be read.'
                : snap.data ?? 'Loading...';
            final intro = widget.intro;
            return SingleChildScrollView(
              controller: _scroll,
              padding: const EdgeInsets.all(16),
              child: Text(
                intro == null ? body : '$intro\n\n$body',
                style: style,
              ),
            );
          },
        );
      },
    ),
  );
}
