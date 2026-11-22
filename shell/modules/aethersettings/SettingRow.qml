// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// One setting: a tonal card with the name and a short line under it; the control sits on the right.
Rectangle {
    id: row
    property string label: ""
    property string desc: ""
    property real reserved: 0
    readonly property real textW: Math.max(40, width - 36 - reserved)
    width: parent ? parent.width : 400
    height: desc.length > 0 ? 68 : 56
    radius: M3.rM
    color: hov.hovered ? M3.surfaceHigh : M3.surfaceC
    Behavior on color { ColorAnimation { duration: 160 } }
    HoverHandler { id: hov }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        T { text: row.label; font.pixelSize: 14; font.weight: Font.Medium; width: Math.min(implicitWidth, row.textW); elide: Text.ElideRight }
        T { kind: "reading"; text: row.desc; visible: row.desc.length > 0; color: M3.inkDim; font.pixelSize: 12; width: Math.min(implicitWidth, row.textW); elide: Text.ElideRight }
    }
}
