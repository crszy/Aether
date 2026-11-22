// DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell) - Copyright (c) 2026 shadowww-dev, MIT.
// Windows changes by dotfiles/dss-port.js.
import QtQuick

// An icon: Material Symbols name ("speed", "memory", ...) or a Nerd Font glyph as fallback.
// filled is kept for the callers but not used: Qt's FILL axis on Material Symbols mixes up glyphs (info drew a
// globe, play_arrow a broken arrow), so every icon is drawn as the font draws it by default.
Text {
    id: mi
    property string name: ""
    property string glyph: ""
    property real size: 20
    property bool filled: false
    text: DashFont.symbols ? name : glyph
    font.family: DashFont.symbols ? "Material Symbols Rounded" : DashFont.nerd
    font.pixelSize: DashFont.symbols ? size * 1.12 : size * 0.92
    color: "#F6F5F4"
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
}
