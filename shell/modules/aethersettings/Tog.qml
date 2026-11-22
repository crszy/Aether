// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

// Material 3 switch: the handle grows when it is on and shows a check mark.
Rectangle {
    id: sw
    property bool checked: false
    signal toggled(bool value)
    readonly property bool sfxOwn: true     // its own on/off sound, not the click
    width: 52
    height: 32
    radius: 16
    color: sw.checked ? M3.primary : M3.surfaceHighest
    border.width: sw.checked ? 0 : 2
    border.color: M3.outline
    Behavior on color { ColorAnimation { duration: 180 } }

    Rectangle {
        id: knob
        readonly property real d: tm.pressed ? 28 : (sw.checked || Prefs.switchIcons ? 24 : 16)
        width: d; height: d; radius: d / 2
        anchors.verticalCenter: parent.verticalCenter
        x: sw.checked ? sw.width - d - 4 : (tm.pressed ? 2 : (Prefs.switchIcons ? 4 : 8))
        color: sw.checked ? M3.inkPrimary : M3.outline
        Behavior on x { NumberAnimation { duration: M3.d(M3.bouncy ? 380 : 260); easing.type: M3.bouncy ? Easing.OutBack : Easing.OutCubic; easing.overshoot: 1.7 * Prefs.bounce } }
        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 180 } }
        Icon { anchors.centerIn: parent; name: "check"; glyph: "\uf00c"; size: 14; color: M3.primary
            opacity: sw.checked && Prefs.switchIcons ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 160 } } }
        Icon { anchors.centerIn: parent; name: "close"; glyph: "\uf00d"; size: 13; color: M3.surfaceHighest
            opacity: !sw.checked && Prefs.switchIcons ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 160 } } }
    }
    MouseArea {
        id: tm
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: { Sfx.play(sw.checked ? "switch-off" : "switch-on"); sw.toggled(!sw.checked) }
    }
}
