import QtQuick
import qs.dms.DCommon.Common

ShaderEffect {
    id: root

    property color swatchColor: "transparent"
    property color secondaryColor: swatchColor
    property color tertiaryColor: swatchColor
    property color ringColor: Style.outline
    property real ringWidth: Style.outlineWidth
    property real minPreviewAlpha: 0.4
    readonly property bool translucent: swatchColor.a < 1

    readonly property real widthPx: width
    readonly property real heightPx: height
    readonly property real ringWidthPx: ringWidth
    readonly property real checkerPx: Math.max(Style.spacingXXS, Math.round(width / 4))
    readonly property real showChecker: translucent ? 1 : 0
    readonly property color fillColor: preview(swatchColor)
    readonly property color secondaryFill: preview(secondaryColor)
    readonly property color tertiaryFill: preview(tertiaryColor)
    readonly property color checkerLight: Style.surfaceContainerLowest
    readonly property color checkerDark: Style.surfaceContainerHighest

    function preview(c) {
        if (c.a >= 1 || c.a <= 0)
            return c;
        return Style.withAlpha(c, Math.max(c.a, minPreviewAlpha));
    }

    blending: true
    fragmentShader: Qt.resolvedUrl("../Shaders/qsb/color_swatch.frag.qsb")
}
