// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

Rectangle {
    id: pill
    property string label: ""
    signal clicked()
    implicitWidth: pillText.implicitWidth + 32
    height: 40
    radius: 20
    color: pillMouse.containsMouse ? M3.surfaceHighest : M3.secondaryContainer
    Behavior on color { ColorAnimation { duration: 140 } }

    T { id: pillText; anchors.centerIn: parent; text: pill.label; font.pixelSize: 13 }
    MouseArea {
        id: pillMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
