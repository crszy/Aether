// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

Rectangle {
    id: f
    property alias text: input.text
    property string placeholder: ""
    signal accepted()
    height: 44
    radius: 12
    color: M3.surfaceHigh
    border.width: input.activeFocus ? 2 : 1
    border.color: input.activeFocus ? M3.primary : M3.outlineVariant

    TextInput {
        id: input
        onTextChanged: if (activeFocus) Sfx.play("type")
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        verticalAlignment: TextInput.AlignVCenter
        color: M3.ink
        font.family: Fonts.main
        font.pixelSize: 13
        clip: true
        selectByMouse: true
        onAccepted: f.accepted()
    }
    T {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 28
        elide: Text.ElideRight
        text: f.placeholder
        color: M3.inkDim
        font.pixelSize: 13
        visible: input.text.length === 0
    }
}