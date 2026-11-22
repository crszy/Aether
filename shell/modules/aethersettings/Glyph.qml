// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

Item {
    id: g
    property string glyph: ""
    property bool active: true
    signal clicked()
    width: 18
    height: 24

    T {
        anchors.centerIn: parent
        text: g.glyph
        font.pixelSize: 14
        opacity: g.active ? 1 : 0.2
        color: gMouse.containsMouse && g.active ? M3.primary : M3.inkDim
    }
    MouseArea {
        id: gMouse
        anchors.fill: parent
        enabled: g.active
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: g.clicked()
    }
}
