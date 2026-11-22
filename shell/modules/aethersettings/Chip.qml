// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

Rectangle {
    id: chip
    property string label: ""
    property bool canLeft: true
    property bool canRight: true
    signal moveLeft()
    signal moveRight()
    signal removed()

    height: 32
    width: chipRow.implicitWidth + 16
    radius: 10
    color: M3.surfaceHigh
    border.width: 1
    border.color: M3.outlineVariant

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: 4
        Glyph { glyph: "‹"; active: chip.canLeft; onClicked: chip.moveLeft() }
        T { text: chip.label; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        Glyph { glyph: "›"; active: chip.canRight; onClicked: chip.moveRight() }
        Glyph { glyph: "×"; onClicked: chip.removed() }
    }
}
