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

/// The notices of every package in the registry, as blocks (a package's
/// name, then each licence paragraph), so the screen can build them lazily.
/// Bundled assets are skipped (they have their own rows).
Future<List<String>> packageLicenceBlocks() async {
  final bundled = {for (final l in bundledLicences) l.name};
  final out = <String>[];
  try {
    await for (final entry in LicenseRegistry.licenses) {
      if (entry.packages.every(bundled.contains)) continue;
      out.add(entry.packages.join(', '));
      for (final p in entry.paragraphs) {
        out.add(p.text);
      }
    }
  } on Object catch (e) {
    debugPrint('licence registry: $e');
  }
  return out.isEmpty ? const ['No package notices were found.'] : out;
}

/// A bundled asset's text as blocks: its details, then its paragraphs.
Future<List<String>> bundledLicenceBlocks(BundledLicence l) async {
  final text = await loadLicenceText(l);
  return [
    '${l.name} ${l.version}',
    l.use,
    for (final p in text.split(RegExp(r'\n\s*\n')))
      if (p.trim().isNotEmpty) p.trim(),
  ];
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
                      load: () => bundledLicenceBlocks(l),
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
                    load: packageLicenceBlocks,
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

/// One licence's text, in blocks the wheel scrolls.
class LicenceTextScreen extends StatefulWidget {
  /// A text screen; [load] is called once, when the screen opens.
  const LicenceTextScreen({required this.title, required this.load, super.key});

  final String title;
  final Future<List<String>> Function() load;

  @override
  State<LicenceTextScreen> createState() => _LicenceTextScreenState();
}

class _LicenceTextScreenState extends State<LicenceTextScreen> {
  final _scroll = ScrollController();
  late final Future<List<String>> _blocks = widget.load();
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
    final pos = _scroll.position;
    _scroll.jumpTo(
      (pos.pixels + e.steps * _line * 2).clamp(0.0, pos.maxScrollExtent),
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
        return Semantics(
          container: true,
          label: widget.title,
          child: FutureBuilder<List<String>>(
            future: _blocks,
            builder: (context, snap) {
              final blocks = snap.hasError
                  ? const ['The licence text could not be read.']
                  : snap.data;
              if (blocks == null) {
                return Semantics(
                  liveRegion: true,
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Text('Loading...', style: style),
                  ),
                );
              }
              return ListView.builder(
                controller: _scroll,
                padding: const EdgeInsets.all(16),
                itemCount: blocks.length,
                itemBuilder: (_, i) => Padding(
                  padding: const EdgeInsets.only(bottom: 12),
                  child: Text(blocks[i], style: style),
                ),
              );
            },
          ),
        );
      },
    ),
  );
}
