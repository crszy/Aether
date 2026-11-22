// Adapted from DarkShadowShell (https://github.com/shadowww-dev/DarkShadowShell), quickshell/rice-settings.
// MIT License, Copyright (c) 2026 shadowww-dev - see LICENSE-DarkShadowShell in this folder.
import QtQuick

Item {
    id: sl
    property real from: 0
    property real to: 1
    property real value: 0
    property real step: 0.01
    property real dragValue: 0
    property bool dragging: false
    readonly property real shown: dragging ? dragValue : (isFinite(value) ? value : from)
    signal released(real v)
    readonly property bool sfxOwn: true     // a soft tick for every step while dragging
    width: 200
    height: Math.max(32, Math.round(30 * Prefs.sliderThick))
    readonly property real pos: to > from ? Math.max(0, Math.min(1, (shown - from) / (to - from))) : 0

    // Material 3 slider: a thick track split by a thin handle, the part before it in your color
    readonly property real hx: (sl.width - 4) * sl.pos
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: 0
        width: Math.max(0, sl.hx - 5)
        height: Math.round(14 * Prefs.sliderThick)
        radius: height / 2
        color: M3.primary
        Behavior on width { enabled: !sl.dragging; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: sl.hx + 9
        width: Math.max(0, sl.width - x)
        height: Math.round(14 * Prefs.sliderThick)
        radius: height / 2
        color: M3.surfaceHighest
        Behavior on x { enabled: !sl.dragging; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        // the end stop
        Rectangle { anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter; width: 4; height: 4; radius: 2; color: M3.primary; visible: parent.width > 14 }
    }
    Rectangle {
        width: 4
        height: Math.round((sl.dragging ? 32 : 28) * Math.max(1, Prefs.sliderThick * 0.9))
        radius: 2
        color: M3.primary
        anchors.verticalCenter: parent.verticalCenter
        x: sl.hx
        Behavior on x { enabled: !sl.dragging; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 120 } }
        // the value while you drag
        Rectangle {
            visible: sl.dragging
            anchors.horizontalCenter: parent.horizontalCenter
            y: -34
            width: Math.max(34, vt.implicitWidth + 16); height: 26; radius: 13
            color: M3.ink
            T { id: vt; anchors.centerIn: parent; font.pixelSize: 11; font.bold: true; color: M3.surface
                text: Math.abs(sl.step) >= 1 ? Math.round(sl.shown) : Number(sl.shown).toFixed(2) }
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function update(mx) {
            const p = Math.max(0, Math.min(1, mx / sl.width))
            const raw = sl.from + p * (sl.to - sl.from)
            const nv = Math.round(raw / sl.step) * sl.step
            if (nv !== sl.dragValue) Sfx.play("slide")
            sl.dragValue = nv
        }
        onPressed: function(mouse) { sl.dragging = true; update(mouse.x) }
        onPositionChanged: function(mouse) { if (sl.dragging) update(mouse.x) }
        onReleased: { sl.dragging = false; sl.released(sl.dragValue) }
    }
}