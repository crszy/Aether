// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// Text in Settings. Follows Settings > Appearance > Fonts > Typography: the text size, heavier headings and
// rounded letters (Google Sans Flex has a roundness axis). Live previews set scaled: false to keep their own size.
Text {
    id: t
    property string kind: "body"
    property bool scaled: kind !== "icon"
    property int basePx: 0
    property int baseW: Font.Normal
    color: M3.ink
    font.family: Fonts.family(kind)
    font.pixelSize: 14
    Component.onCompleted: {
        if (!scaled) return
        basePx = font.pixelSize
        baseW = font.weight
        font.pixelSize = Qt.binding(function() { return Math.max(6, Math.round(t.basePx * Prefs.textScale)) })
        if (kind === "title")
            font.weight = Qt.binding(function() { return Prefs.emphHeadings ? Math.min(Font.Black, Math.max(t.baseW + 100, Font.DemiBold)) : t.baseW })
        t.axes()
    }
    function axes() {
        if (kind === "icon" || kind === "mono" || kind === "code") return
        try { font.variableAxes = Prefs.roundText ? { "ROND": 100 } : {} } catch (e) { }
    }
    Connections { target: Prefs; enabled: t.scaled; function onRoundTextChanged() { t.axes() } }
}
