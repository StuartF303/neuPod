// SPDX-License-Identifier: MPL-2.0

/// The centred panel, for something lost or about to be (UI styles design,
/// "Feedback: three places"). Select or Menu dismisses it; with direct touch
/// its `OK` can be tapped (device view section 6). 452 px wide over a scrim.
library;

import 'package:flutter/widgets.dart';
import 'package:neupod/ui/style/neupod_style.dart';
import 'package:neupod/ui/style/neupod_type.dart';
import 'package:neupod/ui/style/skins.dart';

/// The scrim over the screen with the panel centred on it.
class NeupodPanelScrim extends StatelessWidget {
  const NeupodPanelScrim({super.key, required this.panel});

  final Widget panel;

  @override
  Widget build(BuildContext context) => ColoredBox(
    color: NeupodTheme.of(context).style.panel.scrim,
    child: Center(child: panel),
  );
}

class NeupodPanel extends StatelessWidget {
  const NeupodPanel({
    super.key,
    required this.title,
    required this.message,
    this.okLabel = 'OK',
    this.onOk,
  });

  final String title;
  final String message;
  final String okLabel;
  final VoidCallback? onOk;

  @override
  Widget build(BuildContext context) {
    final theme = NeupodTheme.of(context);
    final skin = theme.style.panel;
    final type = NeupodType(theme.tier);
    final k = theme.tier.k;

    final titleText = Text(
      title,
      style: type.body(skin.title),
      textAlign: TextAlign.center,
    );
    final body = Text(
      message,
      style: neupodTextStyle(22 * k, 28 * k, skin.body),
      textAlign: TextAlign.center,
    );
    final okText = type.body(skin.okText);
    final ok = Semantics(
      container: true,
      button: true,
      label: okLabel,
      onTap: onOk,
      child: ExcludeSemantics(
        child: GestureDetector(
          onTap: onOk,
          child: SizedBox(
            height: skin.okHeight * k,
            child: SkinBox(
              skin: skin.ok,
              child: Center(
                child: Text(
                  okLabel,
                  style: skin.okTextShadows.isEmpty
                      ? okText
                      : okText.copyWith(shadows: skin.okTextShadows),
                ),
              ),
            ),
          ),
        ),
      ),
    );

    final Widget content;
    if (skin.titleStrip != null) {
      // Classic: the title in a strip of the status-bar gradient, the text,
      // then OK as a full-width selection bar.
      content = Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SkinBox(
            skin: skin.titleStrip!,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
              child: titleText,
            ),
          ),
          // No message (the firmware's NULL hint): no second line.
          if (message.isNotEmpty) Padding(padding: skin.padding, child: body),
          ok,
        ],
      );
    } else {
      // Modern: title, text and an accent button, padded, with gaps.
      content = Padding(
        padding: skin.padding,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            titleText,
            if (message.isNotEmpty) ...[SizedBox(height: skin.gap), body],
            SizedBox(height: skin.gap + skin.okMarginTop),
            ok,
          ],
        ),
      );
    }

    return Semantics(
      scopesRoute: true,
      namesRoute: true,
      explicitChildNodes: true,
      label: title,
      child: SizedBox(
        width: PanelSkin.width,
        child: SkinBox(
          skin: skin.box,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(skin.box.radius),
            child: Semantics(
              container: true,
              explicitChildNodes: true,
              label: message,
              child: content,
            ),
          ),
        ),
      ),
    );
  }
}
