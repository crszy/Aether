// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// A Google icon: Material Symbols Rounded by name ("home", "palette" ...) when the font is installed
// (rice-fonts downloads it), otherwise the Nerd Font glyph given as fallback.
Text {
    id: ic
    property string name: ""
    property string glyph: ""
    property real size: 20
    property bool filled: false
    readonly property bool sym: Fonts.symbols && name !== ""
    text: sym ? name : glyph
    font.family: sym ? "Material Symbols Rounded" : Fonts.icons
    font.pixelSize: sym ? Math.round(size * 1.15) : Math.round(size * 0.9)
    // filled is not used: Qt's FILL axis on Material Symbols mixes up glyphs (a globe for info, a broken
    // play arrow), so icons are drawn as the font draws them by default
    color: M3.ink
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
